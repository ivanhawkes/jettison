#include <jettison/net/Client.h>

#include <enet/enet.h>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

#include <jettison/core/Log.h>

#include "Wire.h"

namespace Jettison::Net
{
namespace
{
constexpr Core::Category kNet{"net"};
} // namespace


struct Client::Impl
{
	enum class State : std::uint8_t
	{
		Connecting,
		Admitted,
		Closed
	};

	ClientConfig config;
	ENetHost *pHost = nullptr;
	ENetPeer *pPeer = nullptr;
	State state = State::Connecting;

	void Hello(std::vector<ClientEvent> &events)
	{
		const std::vector<std::byte> body =
		    Wire::EncodeHello({config.protocol.name, config.protocol.version, config.credential});
		ENetPacket *pPacket = Wire::MakePacket(
		    Channel::Reliable, static_cast<MessageKind>(Wire::EngineKind::Hello), body,
		    config.limits.maxMessageBytes);
		if (pPacket != nullptr && Wire::SendPacket(pPeer, Channel::Reliable, pPacket))
			return;
		Core::Log(Core::Severity::Error, kNet, "the hello is over the limit or could not be sent");
		enet_peer_disconnect_now(pPeer, static_cast<enet_uint32>(Reason::Closed));
		Lose(Reason::BadMessage, events);
	}


	void Lose(Reason reason, std::vector<ClientEvent> &events)
	{
		state = State::Closed;
		pPeer = nullptr;
		events.push_back({.type = ClientEvent::Type::Closed, .message = {}, .reason = reason});
	}


	void Take(const ENetEvent &event, std::vector<ClientEvent> &events)
	{
		const std::optional<Channel> channel = Wire::ChannelOf(event.channelID);
		std::optional<Wire::Received> received = Wire::ReadPacket(*event.packet);
		// A server's packet this end cannot read is dropped: a client has nothing to report it to.
		if (!channel || !received)
			return;
		if (received->kind == static_cast<MessageKind>(Wire::EngineKind::Welcome))
		{
			if (state == State::Connecting)
			{
				state = State::Admitted;
				events.push_back({.type = ClientEvent::Type::Admitted, .message = {}});
			}
			return;
		}
		if (state != State::Admitted)
			return;
		if (received->kind == static_cast<MessageKind>(Wire::EngineKind::Snapshot))
		{
			const std::optional<std::uint64_t> step = Wire::ReadStep(received->body);
			if (step)
				events.push_back(
				    {.type = ClientEvent::Type::Snapshot,
				     .message =
				         {*channel, received->kind, {received->body.begin(), received->body.end()}},
				     .step = *step});
			return;
		}
		if (received->kind < kFirstEngineKind)
			events.push_back(
			    {.type = ClientEvent::Type::Message,
			     .message = {
			         *channel, received->kind, {received->body.begin(), received->body.end()}}});
	}
};


Client::Client(ClientConfig config) : m_pImpl{std::make_unique<Impl>()}
{
	Wire::StartEnet();
	ENetAddress address{};
	address.port = config.port;
	if (enet_address_set_host(&address, config.address.c_str()) != 0)
		throw std::runtime_error(std::format("net: '{}' does not resolve", config.address));

	m_pImpl->pHost = enet_host_create(nullptr, 1, Wire::kChannelCount, 0, 0);
	if (m_pImpl->pHost == nullptr)
		throw std::runtime_error("net: cannot open a client socket");
	m_pImpl->pHost->maximumPacketSize = config.limits.maxMessageBytes;
	m_pImpl->pPeer = enet_host_connect(m_pImpl->pHost, &address, Wire::kChannelCount, 0);
	if (m_pImpl->pPeer == nullptr)
	{
		enet_host_destroy(m_pImpl->pHost);
		throw std::runtime_error(
		    std::format("net: cannot connect to {}:{}", config.address, config.port));
	}
	const auto timeout = static_cast<enet_uint32>(config.limits.timeout.count());
	enet_peer_timeout(m_pImpl->pPeer, 0, timeout, timeout);
	m_pImpl->config = std::move(config);
}


Client::~Client()
{
	if (m_pImpl->pPeer != nullptr)
	{
		enet_peer_disconnect_now(m_pImpl->pPeer, static_cast<enet_uint32>(Reason::Closed));
		enet_host_flush(m_pImpl->pHost);
	}
	enet_host_destroy(m_pImpl->pHost);
}


std::vector<ClientEvent> Client::Poll(std::chrono::milliseconds wait)
{
	std::vector<ClientEvent> events;
	if (m_pImpl->state == Impl::State::Closed)
		return events;
	ENetEvent event{};
	auto timeout = static_cast<enet_uint32>(std::max<std::int64_t>(wait.count(), 0));
	int result = 0;
	while (m_pImpl->state != Impl::State::Closed &&
	       (result = enet_host_service(m_pImpl->pHost, &event, timeout)) > 0)
	{
		timeout = 0;
		switch (event.type)
		{
			case ENET_EVENT_TYPE_CONNECT: m_pImpl->Hello(events); break;
			case ENET_EVENT_TYPE_RECEIVE:
				m_pImpl->Take(event, events);
				enet_packet_destroy(event.packet);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				// The server's reason, or Closed for a timeout or a number this build does not know.
				m_pImpl->Lose(
				    event.data <= static_cast<enet_uint32>(Reason::Shutdown)
				        ? static_cast<Reason>(event.data)
				        : Reason::Closed,
				    events);
				break;
			case ENET_EVENT_TYPE_NONE: break;
		}
	}
	if (result < 0)
		Core::Log(Core::Severity::Error, kNet, "the client's socket failed");
	return events;
}


bool Client::Admitted() const
{
	return m_pImpl->state == Impl::State::Admitted;
}


bool Client::Closed() const
{
	return m_pImpl->state == Impl::State::Closed;
}


bool Client::Send(Channel channel, MessageKind kind, std::span<const std::byte> body)
{
	if (m_pImpl->state != Impl::State::Admitted || kind >= kFirstEngineKind)
		return false;
	ENetPacket *pPacket = Wire::MakePacket(channel, kind, body, m_pImpl->config.limits.maxMessageBytes);
	return pPacket != nullptr && Wire::SendPacket(m_pImpl->pPeer, channel, pPacket);
}


void Client::Disconnect()
{
	if (m_pImpl->pPeer != nullptr && m_pImpl->state != Impl::State::Closed)
		enet_peer_disconnect_later(m_pImpl->pPeer, static_cast<enet_uint32>(Reason::Closed));
}
} // namespace Jettison::Net
