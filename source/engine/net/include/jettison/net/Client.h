#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <jettison/net/Protocol.h>

namespace Jettison::Net
{
struct ClientConfig
{
	// A host name or dotted IPv4.
	std::string address{"127.0.0.1"};
	std::uint16_t port{};
	Protocol protocol;

	// The server's public key (Key.h), pinned: a server that cannot open the hello sealed to it
	// refuses as KeyMismatch. An offer whose proof was not sealed by the key's holder closes the client
	// so before the credential is sent, and a welcome not sealed by it closes the client so too.
	std::string serverKey;

	// Opaque to the engine: what the server's verifier is given, sealed.
	std::vector<std::byte> credential;
	Limits limits;
};

struct ClientEvent
{
	enum class Type : std::uint8_t
	{
		Admitted,
		Message,
		Snapshot,
		// Refused, or closed after admission: the reason says which. The client is spent.
		Closed
	};

	Type type{};

	// A Message's; a Snapshot's body, with its kind kept by the engine.
	Message message;

	// The step a Snapshot was taken at.
	std::uint64_t step{};
	Reason reason{Reason::Closed};
};

// The client end of Server (Server.h): connects as it is made, sends the hello once connected, and
// is admitted or refused. To reconnect, make another. Single-threaded, as the server is.
class Client
{
  public:
	// Throws std::invalid_argument for a server key that is not one, std::runtime_error when the
	// address does not resolve.
	explicit Client(ClientConfig config);

	~Client();

	Client(const Client &) = delete;

	Client &operator=(const Client &) = delete;

	// As Server::Poll: what happened, in order, waiting up to wait for the first of it.
	std::vector<ClientEvent> Poll(std::chrono::milliseconds wait = std::chrono::milliseconds{0});

	bool Admitted() const;

	bool Closed() const;

	// Queues a message, sent at the next Poll. False before admission, after closing, for a kind the
	// engine keeps, or for a packet over the limit.
	bool Send(Channel channel, MessageKind kind, std::span<const std::byte> body);

	// Closes the connection, sending what is queued first; the server sees Reason::Closed.
	void Disconnect();

  private:
	struct Impl;
	std::unique_ptr<Impl> m_pImpl;
};
} // namespace Jettison::Net
