#include <jettison/net/Server.h>

#include <enet/enet.h>

#include <algorithm>
#include <deque>
#include <format>
#include <map>
#include <stdexcept>
#include <utility>

#include <jettison/core/Log.h>

#include "Wire.h"

namespace Jettison::Net
{
namespace
{
constexpr Core::Category kNet{"net"};

using Clock = std::chrono::steady_clock;
} // namespace


struct Server::Impl
{
	struct Session
	{
		SessionId id{};
		ENetPeer *pPeer = nullptr;
		bool admitted = false;
		Clock::time_point connectedAt;
		std::optional<Clock::time_point> lastSnapshot;
		std::deque<Message> inbound;
	};

	ServerConfig config;
	Verifier verifier;
	ENetHost *pHost = nullptr;
	// A session's peer points back at its node, which the map never moves.
	std::map<SessionId, Session> sessions;
	std::uint32_t nextId = 1;
	std::size_t admittedCount = 0;

	Session *Find(SessionId id, bool admitted)
	{
		const auto found = sessions.find(id);
		if (found == sessions.end() || found->second.admitted != admitted)
			return nullptr;
		return &found->second;
	}


	// The peer is left to finish closing in ENet, which reports nothing more of it to the game.
	void Close(Session &session, Reason reason, std::vector<ServerEvent> *pEvents)
	{
		enet_peer_disconnect_later(session.pPeer, static_cast<enet_uint32>(reason));
		session.pPeer->data = nullptr;
		if (session.admitted)
		{
			--admittedCount;
			if (pEvents != nullptr)
				pEvents->push_back({ServerEvent::Type::Closed, session.id, reason});
		}
		sessions.erase(session.id);
	}


	void Greet(Session &session, std::span<const std::byte> body, std::vector<ServerEvent> &events)
	{
		const std::optional<Wire::Hello> hello = Wire::DecodeHello(body);
		if (!hello)
			return Close(session, Reason::BadMessage, nullptr);
		if (hello->protocol != config.protocol)
			return Close(session, Reason::UnknownProtocol, nullptr);
		if (std::ranges::find(config.versions, hello->version) == config.versions.end())
			return Close(session, Reason::VersionMismatch, nullptr);
		if (admittedCount >= config.maxSessions)
			return Close(session, Reason::ServerFull, nullptr);
		if (!verifier(session.id, hello->credential))
			return Close(session, Reason::BadCredential, nullptr);

		ENetPacket *pWelcome = Wire::MakePacket(
		    Channel::Reliable, static_cast<MessageKind>(Wire::EngineKind::Welcome), {},
		    config.limits.maxMessageBytes);
		if (pWelcome == nullptr || !Wire::SendPacket(session.pPeer, Channel::Reliable, pWelcome))
			return Close(session, Reason::Closed, nullptr);
		session.admitted = true;
		++admittedCount;
		events.push_back({ServerEvent::Type::Admitted, session.id, Reason::Closed});
	}


	void Take(Session &session, const ENetEvent &event, std::vector<ServerEvent> &events)
	{
		const std::optional<Channel> channel = Wire::ChannelOf(event.channelID);
		const std::optional<Wire::Received> received = Wire::ReadPacket(*event.packet);
		if (!channel || !received)
			return Close(session, Reason::BadMessage, &events);

		if (!session.admitted)
		{
			if (*channel != Channel::Reliable ||
			    received->kind != static_cast<MessageKind>(Wire::EngineKind::Hello))
				return Close(session, Reason::BadMessage, nullptr);
			return Greet(session, received->body, events);
		}

		if (received->kind >= kFirstEngineKind)
			return Close(session, Reason::BadMessage, &events);
		if (session.inbound.size() >= config.maxInbound)
			return Close(session, Reason::InboundOverflow, &events);
		session.inbound.push_back(
		    {*channel, received->kind, {received->body.begin(), received->body.end()}});
	}


	void Handle(const ENetEvent &event, std::vector<ServerEvent> &events)
	{
		auto *pSession = static_cast<Session *>(event.peer->data);
		switch (event.type)
		{
			case ENET_EVENT_TYPE_CONNECT:
			{
				const SessionId id{nextId++};
				Session &session = sessions[id];
				session.id = id;
				session.pPeer = event.peer;
				session.connectedAt = Clock::now();
				event.peer->data = &session;
				const auto timeout = static_cast<enet_uint32>(config.limits.timeout.count());
				enet_peer_timeout(event.peer, 0, timeout, timeout);
				break;
			}
			case ENET_EVENT_TYPE_RECEIVE:
				if (pSession != nullptr)
					Take(*pSession, event, events);
				enet_packet_destroy(event.packet);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				if (pSession != nullptr)
				{
					if (pSession->admitted)
					{
						--admittedCount;
						events.push_back({ServerEvent::Type::Closed, pSession->id, Reason::Closed});
					}
					sessions.erase(pSession->id);
					event.peer->data = nullptr;
				}
				break;
			case ENET_EVENT_TYPE_NONE: break;
		}
	}
};


Server::Server(ServerConfig config, Verifier verifier) : m_pImpl{std::make_unique<Impl>()}
{
	Wire::StartEnet();
	if (!verifier)
		throw std::invalid_argument("net: a server needs a verifier");
	ENetAddress address{};
	address.host = ENET_HOST_ANY;
	address.port = config.port;
	if (!config.address.empty() && enet_address_set_host_ip(&address, config.address.c_str()) != 0)
		throw std::runtime_error(std::format("net: '{}' is not an IPv4 address", config.address));

	m_pImpl->pHost =
	    enet_host_create(&address, config.maxSessions + config.maxPending, Wire::kChannelCount, 0, 0);
	if (m_pImpl->pHost == nullptr)
		throw std::runtime_error(
		    std::format("net: cannot listen on {}:{}", config.address, config.port));
	// A longer packet is dropped by ENet before it is reassembled.
	m_pImpl->pHost->maximumPacketSize = config.limits.maxMessageBytes;
	m_pImpl->config = std::move(config);
	m_pImpl->verifier = std::move(verifier);
}


Server::~Server()
{
	if (m_pImpl->pHost == nullptr)
		return;
	for (auto &[id, session] : m_pImpl->sessions)
		enet_peer_disconnect_now(session.pPeer, static_cast<enet_uint32>(Reason::Shutdown));
	enet_host_destroy(m_pImpl->pHost);
}


std::uint16_t Server::Port() const
{
	return m_pImpl->pHost->address.port;
}


std::vector<ServerEvent> Server::Poll(std::chrono::milliseconds wait)
{
	std::vector<ServerEvent> events;
	const Clock::time_point now = Clock::now();
	for (auto next = m_pImpl->sessions.begin(); next != m_pImpl->sessions.end();)
	{
		Impl::Session &session = (next++)->second;
		if (!session.admitted && now - session.connectedAt > m_pImpl->config.handshakeTimeout)
			m_pImpl->Close(session, Reason::HandshakeTimeout, nullptr);
	}

	ENetEvent event{};
	auto timeout = static_cast<enet_uint32>(std::max<std::int64_t>(wait.count(), 0));
	int result = 0;
	while ((result = enet_host_service(m_pImpl->pHost, &event, timeout)) > 0)
	{
		m_pImpl->Handle(event, events);
		timeout = 0;
	}
	if (result < 0)
		Core::Log(Core::Severity::Error, kNet, "the server's socket failed");
	return events;
}


std::optional<Message> Server::Receive(SessionId session)
{
	Impl::Session *pSession = m_pImpl->Find(session, true);
	if (pSession == nullptr || pSession->inbound.empty())
		return std::nullopt;
	Message message = std::move(pSession->inbound.front());
	pSession->inbound.pop_front();
	return message;
}


bool Server::Send(SessionId session, Channel channel, MessageKind kind, std::span<const std::byte> body)
{
	Impl::Session *pSession = m_pImpl->Find(session, true);
	if (pSession == nullptr || kind >= kFirstEngineKind)
		return false;
	ENetPacket *pPacket = Wire::MakePacket(channel, kind, body, m_pImpl->config.limits.maxMessageBytes);
	return pPacket != nullptr && Wire::SendPacket(pSession->pPeer, channel, pPacket);
}


void Server::SendSnapshots(std::uint64_t step, const SnapshotView &view)
{
	const Clock::time_point now = Clock::now();
	// By id, so a view that disconnects a session leaves the rest to be asked.
	for (const SessionId id : Sessions())
	{
		Impl::Session *pSession = m_pImpl->Find(id, true);
		if (pSession == nullptr || (pSession->lastSnapshot &&
		                            now - *pSession->lastSnapshot < m_pImpl->config.snapshotInterval))
			continue;
		const std::optional<std::vector<std::byte>> body = view(id);
		pSession = m_pImpl->Find(id, true);
		if (!body || pSession == nullptr)
			continue;
		ENetPacket *pPacket = Wire::MakePacket(
		    Channel::Unreliable, static_cast<MessageKind>(Wire::EngineKind::Snapshot), *body,
		    m_pImpl->config.limits.maxMessageBytes, step);
		if (pPacket == nullptr)
		{
			Core::Log(
			    Core::Severity::Warning, kNet,
			    "session {}'s snapshot at step {} is {} bytes, over the limit",
			    static_cast<std::uint32_t>(id), step, body->size());
			continue;
		}
		if (Wire::SendPacket(pSession->pPeer, Channel::Unreliable, pPacket))
			pSession->lastSnapshot = now;
	}
}


void Server::Disconnect(SessionId session, Reason reason)
{
	if (Impl::Session *pSession = m_pImpl->Find(session, true))
		m_pImpl->Close(*pSession, reason, nullptr);
}


std::vector<SessionId> Server::Sessions() const
{
	std::vector<SessionId> ids;
	for (const auto &[id, session] : m_pImpl->sessions)
		if (session.admitted)
			ids.push_back(id);
	return ids;
}
} // namespace Jettison::Net
