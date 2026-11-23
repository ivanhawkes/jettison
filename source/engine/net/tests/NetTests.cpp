#include <doctest/doctest.h>
#include <enet/enet.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/net/Client.h>
#include <jettison/net/Server.h>
#include <jettison/net/Token.h>

using namespace Jettison::Net;
using namespace std::chrono_literals;

namespace
{
std::vector<std::byte> Bytes(std::string_view text)
{
	std::vector<std::byte> bytes;
	for (const char character : text)
		bytes.push_back(static_cast<std::byte>(character));
	return bytes;
}


std::string Text(std::span<const std::byte> bytes)
{
	std::string text;
	for (const std::byte byte : bytes)
		text.push_back(static_cast<char>(byte));
	return text;
}


// A server on an ephemeral loopback port that admits the credential "good", recording each admitted
// session's credential as the game's actor would be.
struct Fixture
{
	explicit Fixture(ServerConfig config = {})
	{
		config.port = 0;
		config.protocol = "test";
		config.versions = {2, 3};
		config.handshakeTimeout = 300ms;
		pServer = std::make_unique<Server>(
		    config,
		    [this](SessionId session, std::span<const std::byte> credential)
		    {
			    if (Text(credential) != "good" && Text(credential) != "other")
				    return false;
			    actors[session] = Text(credential);
			    return true;
		    });
	}


	ClientConfig Config(std::string credential = "good", std::uint32_t version = 3) const
	{
		ClientConfig config;
		config.port = pServer->Port();
		config.protocol = {"test", version};
		config.credential = Bytes(credential);
		return config;
	}


	// Polls both ends until the condition holds, or fails the case after two seconds.
	void Until(const std::function<bool()> &condition, std::vector<Client *> clients)
	{
		const auto deadline = std::chrono::steady_clock::now() + 2s;
		while (!condition())
		{
			REQUIRE(std::chrono::steady_clock::now() < deadline);
			for (const ServerEvent &event : pServer->Poll(1ms))
				serverEvents.push_back(event);
			for (Client *pClient : clients)
				for (ClientEvent &event : pClient->Poll())
					clientEvents[pClient].push_back(std::move(event));
		}
	}


	std::optional<ClientEvent> Last(Client &client, ClientEvent::Type type) const
	{
		const auto found = clientEvents.find(&client);
		if (found == clientEvents.end())
			return std::nullopt;
		for (const ClientEvent &event : std::views::reverse(found->second))
			if (event.type == type)
				return event;
		return std::nullopt;
	}


	Reason ClosedWith(Client &client)
	{
		Until([&] { return client.Closed(); }, {&client});
		return Last(client, ClientEvent::Type::Closed)->reason;
	}


	std::unique_ptr<Server> pServer;
	std::map<SessionId, std::string> actors;
	std::vector<ServerEvent> serverEvents;
	std::map<Client *, std::vector<ClientEvent>> clientEvents;
};
} // namespace


TEST_CASE("a token matches its own digest and no other")
{
	const std::string token = GenerateToken();
	CHECK(token.size() == 64);
	CHECK(token.find_first_not_of("0123456789abcdef") == std::string::npos);
	CHECK(GenerateToken() != token);

	const std::string digest = DigestToken(token);
	CHECK(digest.size() == 64);
	CHECK(digest != token);
	CHECK(TokenMatches(token, digest));
	CHECK_FALSE(TokenMatches(GenerateToken(), digest));
	CHECK_FALSE(TokenMatches(token, digest.substr(1)));
	CHECK_FALSE(TokenMatches(token, ""));
}


TEST_CASE("an admitted client exchanges messages and is sent its own snapshot at a bounded rate")
{
	ServerConfig config;
	config.snapshotInterval = 10min;
	Fixture fixture{config};
	Client first{fixture.Config("good")};
	Client second{fixture.Config("other", 2)};
	fixture.Until([&] { return first.Admitted() && second.Admitted(); }, {&first, &second});
	REQUIRE(fixture.pServer->Sessions().size() == 2);
	REQUIRE(fixture.actors.size() == 2);
	CHECK(
	    std::ranges::count(fixture.serverEvents, ServerEvent::Type::Admitted, &ServerEvent::type) == 2);

	const std::vector<std::byte> request = Bytes("buy");
	CHECK(first.Send(Channel::Reliable, 7, request));
	CHECK_FALSE(first.Send(Channel::Reliable, kFirstEngineKind, request));
	const SessionId firstSession = fixture.actors.begin()->second == "good"
	                                   ? fixture.actors.begin()->first
	                                   : fixture.actors.rbegin()->first;
	std::optional<Message> received;
	fixture.Until(
	    [&]
	    {
		    received = fixture.pServer->Receive(firstSession);
		    return received.has_value();
	    },
	    {&first, &second});
	CHECK(received->kind == 7);
	CHECK(received->channel == Channel::Reliable);
	CHECK(Text(received->body) == "buy");

	CHECK(fixture.pServer->Send(firstSession, Channel::Reliable, 8, Bytes("done")));
	fixture.Until(
	    [&] { return fixture.Last(first, ClientEvent::Type::Message).has_value(); }, {&first});
	CHECK(fixture.Last(first, ClientEvent::Type::Message)->message.kind == 8);

	// Each session's view is its own actor's, and the second call inside the interval sends nothing.
	int views = 0;
	const SnapshotView view = [&](SessionId session) -> std::optional<std::vector<std::byte>>
	{
		++views;
		return Bytes("seen by " + fixture.actors.at(session));
	};
	fixture.pServer->SendSnapshots(41, view);
	fixture.pServer->SendSnapshots(42, view);
	CHECK(views == 2);
	fixture.Until(
	    [&]
	    {
		    return fixture.Last(first, ClientEvent::Type::Snapshot) &&
		           fixture.Last(second, ClientEvent::Type::Snapshot);
	    },
	    {&first, &second});
	const ClientEvent snapshot = *fixture.Last(first, ClientEvent::Type::Snapshot);
	CHECK(snapshot.step == 41);
	CHECK(Text(snapshot.message.body) == "seen by good");
	CHECK(Text(fixture.Last(second, ClientEvent::Type::Snapshot)->message.body) == "seen by other");
}


TEST_CASE("a hello is refused for its protocol, its version, its credential or a full server")
{
	ServerConfig config;
	config.maxSessions = 1;
	Fixture fixture{config};

	ClientConfig unknown = fixture.Config();
	unknown.protocol.name = "other";
	Client stranger{unknown};
	CHECK(fixture.ClosedWith(stranger) == Reason::UnknownProtocol);

	Client old{fixture.Config("good", 1)};
	CHECK(fixture.ClosedWith(old) == Reason::VersionMismatch);

	Client impostor{fixture.Config("bad")};
	CHECK(fixture.ClosedWith(impostor) == Reason::BadCredential);
	CHECK(fixture.actors.empty());
	CHECK(fixture.serverEvents.empty());

	// Reconnecting after a refusal is a new session, and one more than the server allows is refused.
	Client player{fixture.Config()};
	fixture.Until([&] { return player.Admitted(); }, {&player});
	Client late{fixture.Config("other")};
	CHECK(fixture.ClosedWith(late) == Reason::ServerFull);
	CHECK(player.Admitted());
}


TEST_CASE("a session past its inbound bound is closed, and the game's reason reaches its client")
{
	ServerConfig config;
	config.maxInbound = 4;
	Fixture fixture{config};
	Client flood{fixture.Config()};
	fixture.Until([&] { return flood.Admitted(); }, {&flood});
	for (int message = 0; message < 5; ++message)
		CHECK(flood.Send(Channel::Reliable, 1, Bytes("spam")));
	CHECK(fixture.ClosedWith(flood) == Reason::InboundOverflow);
	REQUIRE(fixture.serverEvents.size() == 2);
	CHECK(fixture.serverEvents.back().type == ServerEvent::Type::Closed);
	CHECK(fixture.serverEvents.back().reason == Reason::InboundOverflow);
	CHECK(fixture.pServer->Sessions().empty());

	Client again{fixture.Config()};
	fixture.Until([&] { return again.Admitted(); }, {&again});
	const SessionId session = fixture.pServer->Sessions().front();
	CHECK(session != fixture.serverEvents.front().session);
	fixture.pServer->Disconnect(session, Reason::Revoked);
	CHECK(fixture.pServer->Sessions().empty());
	CHECK(fixture.ClosedWith(again) == Reason::Revoked);

	Client leaving{fixture.Config()};
	fixture.Until([&] { return leaving.Admitted(); }, {&leaving});
	leaving.Disconnect();
	fixture.Until([&] { return fixture.pServer->Sessions().empty(); }, {&leaving});
	CHECK(fixture.serverEvents.back().reason == Reason::Closed);
}


TEST_CASE("a message over the limit is not sent")
{
	ServerConfig config;
	config.limits.maxMessageBytes = 128;
	Fixture fixture{config};
	ClientConfig clientConfig = fixture.Config();
	clientConfig.limits.maxMessageBytes = 128;
	Client client{clientConfig};
	fixture.Until([&] { return client.Admitted(); }, {&client});
	CHECK(client.Send(Channel::Unreliable, 1, std::vector<std::byte>(126)));
	CHECK_FALSE(client.Send(Channel::Unreliable, 1, std::vector<std::byte>(127)));
	const SessionId session = fixture.pServer->Sessions().front();
	CHECK_FALSE(fixture.pServer->Send(session, Channel::Reliable, 1, std::vector<std::byte>(127)));
	// A snapshot's step counts against the limit too.
	fixture.pServer->SendSnapshots(1, [](SessionId) { return std::vector<std::byte>(119); });
	CHECK(fixture.pServer->Send(session, Channel::Reliable, 1, std::vector<std::byte>(4)));
	fixture.Until(
	    [&] { return fixture.Last(client, ClientEvent::Type::Message).has_value(); }, {&client});
	CHECK_FALSE(fixture.Last(client, ClientEvent::Type::Snapshot));
}


TEST_CASE("a message several datagrams long arrives whole on either channel")
{
	const auto filled = [](std::size_t bytes)
	{
		std::vector<std::byte> body(bytes);
		for (std::size_t index = 0; index < body.size(); ++index)
			body[index] = static_cast<std::byte>(index * 31 >> 3);
		return body;
	};
	ServerConfig config;
	config.limits.maxMessageBytes = std::size_t{1024} * 1024 + 16;
	Fixture fixture{config};
	ClientConfig clientConfig = fixture.Config();
	clientConfig.limits = config.limits;
	Client client{clientConfig};
	fixture.Until([&] { return client.Admitted(); }, {&client});
	const SessionId session = fixture.pServer->Sessions().front();

	// Unreliable fragments are sent at once and wait in the receiver's socket buffer, ENet's 256 KiB:
	// on loopback 256 KiB arrived twenty times of twenty and 384 KiB never (2026-11-24).
	const std::vector<std::byte> snapshot = filled(std::size_t{192} * 1024);
	fixture.pServer->SendSnapshots(5, [&](SessionId) { return std::vector<std::byte>{snapshot}; });
	fixture.Until(
	    [&] { return fixture.Last(client, ClientEvent::Type::Snapshot).has_value(); }, {&client});
	CHECK(fixture.Last(client, ClientEvent::Type::Snapshot)->message.body == snapshot);

	// Reliable fragments are paced by ENet's window and sent again when lost.
	const std::vector<std::byte> request = filled(std::size_t{1024} * 1024);
	CHECK(client.Send(Channel::Reliable, 1, request));
	std::optional<Message> received;
	fixture.Until(
	    [&]
	    {
		    received = fixture.pServer->Receive(session);
		    return received.has_value();
	    },
	    {&client});
	CHECK(received->body == request);
}


// A peer that is not this engine's client: what ENet lets anyone send.
TEST_CASE("a connection that sends no hello, or anything else before one, is closed")
{
	Fixture fixture;
	REQUIRE(enet_initialize() == 0);
	ENetHost *pHost = enet_host_create(nullptr, 2, 2, 0, 0);
	REQUIRE(pHost != nullptr);
	ENetAddress address{};
	REQUIRE(enet_address_set_host_ip(&address, "127.0.0.1") == 0);
	address.port = fixture.pServer->Port();
	ENetPeer *pSilent = enet_host_connect(pHost, &address, 2, 0);
	ENetPeer *pRude = enet_host_connect(pHost, &address, 2, 0);

	std::map<ENetPeer *, std::optional<enet_uint32>> closed{{pSilent, {}}, {pRude, {}}};
	fixture.Until(
	    [&]
	    {
		    ENetEvent event{};
		    while (enet_host_service(pHost, &event, 0) > 0)
		    {
			    if (event.type == ENET_EVENT_TYPE_CONNECT && event.peer == pRude)
			    {
				    const std::array<enet_uint8, 3> message{1, 0, 0};
				    enet_peer_send(
				        pRude, 0,
				        enet_packet_create(message.data(), message.size(), ENET_PACKET_FLAG_RELIABLE));
			    }
			    if (event.type == ENET_EVENT_TYPE_DISCONNECT)
				    closed[event.peer] = event.data;
			    if (event.type == ENET_EVENT_TYPE_RECEIVE)
				    enet_packet_destroy(event.packet);
		    }
		    return closed[pSilent] && closed[pRude];
	    },
	    {});
	CHECK(*closed[pRude] == static_cast<enet_uint32>(Reason::BadMessage));
	CHECK(*closed[pSilent] == static_cast<enet_uint32>(Reason::HandshakeTimeout));
	CHECK(fixture.serverEvents.empty());
	enet_host_destroy(pHost);
}
