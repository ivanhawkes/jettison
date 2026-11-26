// A game's smallest build with nothing of space in it: a race along a track, composed into the
// public host and run through its whole life: definitions, commands, fixed steps, a save, a load
// and teardown. The world holds one component, Token, and the game registers no transform, galaxy
// or star system. Then it serves the race over loopback: players with bearer tokens, refused for a
// bad token, a wrong version, another server's key and a flood, reconnecting, placing their pieces by
// request and seeing only their own in a snapshot. It exits non-zero at the first check that fails.
#include <jettison/core/Assert.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/host/Session.h>
#include <jettison/net/Client.h>
#include <jettison/net/Key.h>
#include <jettison/net/Server.h>
#include <jettison/net/Token.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Binary.h>
#include <jettison/sim/CommandQueue.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace Jettison;

namespace
{
// What a piece is, from the content's files.
struct Piece
{
	std::string displayName;
	int stride{};
};

// A piece on the track; the save refers to its definition by name.
struct Token
{
	std::string piece;
	int square{};
};

struct Place
{
	std::string piece;
};


class Race : public Host::Game
{
  public:
	// Reflection is process-wide, so a second session finds the types already registered.
	void RegisterTypes() override
	{
		static const bool registered = []
		{
			using Reflect::Attribute;
			Reflect::RegisterType<Piece>("Consumer.Piece")
			    .Field<&Piece::displayName>("displayName", Attribute::Saved)
			    .Field<&Piece::stride>("stride", Attribute::Saved);
			Reflect::RegisterType<Token>("Consumer.Token")
			    .Field<&Token::piece>("piece", Attribute::Saved)
			    .Field<&Token::square>("square", Attribute::Saved);
			Reflect::RegisterType<Place>("Consumer.Place")
			    .Field<&Place::piece>("piece", Attribute::Saved);
			return true;
		}();
		(void)registered;
	}


	void LoadContent(
	    const Vfs::FileSystem & /*files*/, std::span<const Host::Package> /*packages*/,
	    Sim::DefinitionRegistry &definitions) override
	{
		definitions.Load<Piece>("defs/pieces", {1});
	}


	void RegisterWorld(Sim::SimWorld &world, const Sim::DefinitionRegistry &definitions) override
	{
		world.RegisterComponent<Token>();
		world.RegisterCommand<Place>(
		    [&definitions](Sim::SimWorld &target, const Place &command)
		    {
			    JETTISON_VERIFY(definitions.Find<Piece>(command.piece) != nullptr);
			    target.Registry().emplace<Token>(target.Registry().create(), command.piece, 0);
		    });

		// A piece goes its stride, and a die the world's generator rolls may add a square.
		world.RegisterSystem(
		    {"advance",
		     Ecs::TickPhase::Simulation,
		     Ecs::Access::Of<>().Writing<Token>(),
		     {},
		     [&definitions](entt::registry &registry, double)
		     {
			     auto &random = registry.ctx().get<Math::Random>();
			     for (auto [entity, token] : registry.view<Token>().each())
				     token.square += definitions.Find<Piece>(token.piece)->stride + random.Range(0, 1);
		     }});
	}
};


std::unique_ptr<Host::Session> Start()
{
	return std::make_unique<Host::Session>(
	    Host::Options{{{"race", CONSUMER_CONTENT}}, 1.0, 7}, std::make_unique<Race>());
}


bool Expect(bool condition, const char *what)
{
	if (!condition)
		std::fprintf(stderr, "consumer: %s\n", what);
	return condition;
}


void Run(Sim::SimWorld &world, int steps)
{
	for (int step = 0; step < steps; ++step)
		world.Step();
}


int Play()
{
	const std::unique_ptr<Host::Session> pFirst = Start();
	Sim::SimWorld &world = pFirst->World();
	if (!Expect(pFirst->Definitions().Find<Piece>("hare")->stride == 3, "the definition did not load"))
		return EXIT_FAILURE;

	world.Commands().Push(Place{"hare"});
	world.Commands().Push(Place{"tortoise"});
	Run(world, 5);
	const std::vector<std::byte> save = world.Save();
	const std::uint64_t hashAtSave = world.StateHash();
	Run(world, 5);
	const std::uint64_t hashAtEnd = world.StateHash();

	// The same seed and commands give the same state in another session.
	const std::unique_ptr<Host::Session> pSecond = Start();
	pSecond->World().Commands().Push(Place{"hare"});
	pSecond->World().Commands().Push(Place{"tortoise"});
	Run(pSecond->World(), 10);

	// A third loads the save and carries on from the same point.
	const std::unique_ptr<Host::Session> pThird = Start();
	pThird->World().Load(save);
	if (!Expect(pThird->World().StateHash() == hashAtSave, "a load did not restore the state"))
		return EXIT_FAILURE;
	Run(pThird->World(), 5);

	if (!Expect(world.Registry().view<Token>().size() == 2, "the commands placed no pieces") ||
	    !Expect(pSecond->World().StateHash() == hashAtEnd, "the same run gave another state") ||
	    !Expect(pThird->World().StateHash() == hashAtEnd, "the loaded run gave another state"))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}


// The race's protocol: a request to place the player's own piece, and a view of the pieces a player
// may see, in the binary archive.
constexpr Net::MessageKind kPlaceMine = 1;
constexpr std::string_view kViewFormat = "consumer.view";
constexpr std::uint32_t kVersion = 1;


std::vector<std::byte> Bytes(const std::string &text)
{
	const auto *pFirst = reinterpret_cast<const std::byte *>(text.data());
	return {pFirst, pFirst + text.size()};
}


// Both ends polled until the condition holds of what the client reported, for at most two seconds.
bool Pump(
    Net::Server &server, Net::Client &client,
    const std::function<bool(std::vector<Net::ClientEvent> &)> &condition)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
	std::vector<Net::ClientEvent> events;
	while (!condition(events))
	{
		if (std::chrono::steady_clock::now() > deadline)
			return false;
		(void)server.Poll(std::chrono::milliseconds{1});
		events = client.Poll();
	}
	return true;
}


// What closed the client: Closed itself if it is still open after the wait.
Net::Reason ClosedWith(Net::Server &server, Net::Client &client)
{
	std::optional<Net::Reason> reason;
	Pump(
	    server, client,
	    [&](std::vector<Net::ClientEvent> &events)
	    {
		    for (const Net::ClientEvent &event : events)
			    if (event.type == Net::ClientEvent::Type::Closed)
				    reason = event.reason;
		    return reason.has_value();
	    });
	return reason.value_or(Net::Reason::Closed);
}


int Serve()
{
	const std::unique_ptr<Host::Session> pSession = Start();
	Sim::SimWorld &world = pSession->World();
	world.Commands().Push(Place{"tortoise"});

	// The operator made the server's key and gave its players the public half to pin, then issued
	// the hare's player a token and kept its digest; the session's actor is the verifier's to
	// record, so no request names a piece.
	const Net::ServerKey key = Net::GenerateServerKey();
	const std::string hareToken = Net::GenerateToken();
	const std::map<std::string, std::string> digests{{Net::DigestToken(hareToken), "hare"}};
	std::map<Net::SessionId, std::string> actors;
	Net::ServerConfig config;
	config.protocol = "race";
	config.versions = {kVersion};
	config.maxInbound = 2;
	config.secretKey = key.secretKey;
	Net::Server server{
	    config, [&](Net::SessionId session, std::span<const std::byte> credential)
	    {
		    const std::string token{
		        reinterpret_cast<const char *>(credential.data()), credential.size()};
		    for (const auto &[digest, piece] : digests)
			    if (Net::TokenMatches(token, digest))
			    {
				    actors[session] = piece;
				    return true;
			    }
		    return false;
	    }};

	Net::ClientConfig player;
	player.port = server.Port();
	player.protocol = {"race", kVersion};
	player.serverKey = Net::GenerateServerKey().publicKey;
	player.credential = Bytes(hareToken);
	Net::Client astray{player};
	if (!Expect(
	        ClosedWith(server, astray) == Net::Reason::KeyMismatch,
	        "a client pinning another server's key was not refused"))
		return 1;
	player.serverKey = key.publicKey;
	player.credential = Bytes(Net::GenerateToken());
	Net::Client impostor{player};
	if (!Expect(
	        ClosedWith(server, impostor) == Net::Reason::BadCredential, "a bad token was not refused"))
		return EXIT_FAILURE;

	player.credential = Bytes(hareToken);
	player.protocol.version = kVersion + 1;
	Net::Client stale{player};
	if (!Expect(
	        ClosedWith(server, stale) == Net::Reason::VersionMismatch,
	        "a wrong version was not refused"))
		return EXIT_FAILURE;

	player.protocol.version = kVersion;
	Net::Client flood{player};
	if (!Expect(
	        Pump(server, flood, [&](auto &) { return flood.Admitted(); }),
	        "the player was not admitted"))
		return EXIT_FAILURE;
	for (int request = 0; request < 3; ++request)
		(void)flood.Send(Net::Channel::Reliable, kPlaceMine, {});
	if (!Expect(ClosedWith(server, flood) == Net::Reason::InboundOverflow, "a flood was not closed"))
		return EXIT_FAILURE;

	// Reconnected, the player places their piece; the world steps between polls.
	Net::Client client{player};
	if (!Expect(
	        Pump(server, client, [&](auto &) { return client.Admitted(); }),
	        "the player did not reconnect") ||
	    !Expect(client.Send(Net::Channel::Reliable, kPlaceMine, {}), "the request was not sent"))
		return EXIT_FAILURE;
	const Net::SessionId session = server.Sessions().front();
	const bool placed = Pump(
	    server, client,
	    [&](auto &)
	    {
		    const std::optional<Net::Message> request = server.Receive(session);
		    if (request && request->kind == kPlaceMine)
			    world.Commands().Push(Place{actors.at(session)});
		    return request.has_value();
	    });
	Run(world, 3);
	if (!Expect(placed && world.Registry().view<Token>().size() == 2, "the request placed no piece"))
		return EXIT_FAILURE;

	server.SendSnapshots(
	    world.Clock().Step(),
	    [&](Net::SessionId viewer) -> std::optional<std::vector<std::byte>>
	    {
		    std::vector<Token> visible;
		    for (auto [entity, token] : world.Registry().view<Token>().each())
			    if (token.piece == actors.at(viewer))
				    visible.push_back(token);
		    Serialise::BinaryWriter writer{kViewFormat};
		    writer.Write(visible);
		    return writer.Finish({1});
	    });
	std::optional<Net::ClientEvent> snapshot;
	Pump(
	    server, client,
	    [&](std::vector<Net::ClientEvent> &events)
	    {
		    for (Net::ClientEvent &event : events)
			    if (event.type == Net::ClientEvent::Type::Snapshot)
				    snapshot = std::move(event);
		    return snapshot.has_value();
	    });
	if (!Expect(snapshot.has_value(), "no snapshot arrived"))
		return EXIT_FAILURE;
	std::vector<Token> seen;
	Serialise::BinaryReader reader{snapshot->message.body, kViewFormat, {1}};
	reader.Read(seen);
	reader.End();
	if (!Expect(
	        seen.size() == 1 && seen.front().piece == "hare",
	        "the snapshot was not the player's own") ||
	    !Expect(snapshot->step == world.Clock().Step(), "the snapshot named another step"))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
} // namespace


int main()
{
	try
	{
		// The sessions are gone, world before game, when Play returns.
		if (Play() != EXIT_SUCCESS || Serve() != EXIT_SUCCESS)
			return EXIT_FAILURE;
	}
	catch (const std::exception &error)
	{
		std::fprintf(stderr, "consumer: %s\n", error.what());
		return EXIT_FAILURE;
	}
	std::puts(
	    "consumer: a race defined, stepped, saved, loaded, served and torn down through Jettison::Host "
	    "and Jettison::Net");
	return EXIT_SUCCESS;
}
