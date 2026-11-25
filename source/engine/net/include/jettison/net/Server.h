#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <jettison/net/Protocol.h>

namespace Jettison::Net
{
// One connection, numbered from 1 as connections arrive and never reused by the server; a session is
// the game's once admitted. Reconnecting is a new session: what a session stands for (the game's
// actor) is the verifier's to record.
enum class SessionId : std::uint32_t
{
};

struct ServerConfig
{
	// Dotted IPv4, or empty for every interface. Port 0 takes an ephemeral one (Server::Port).
	std::string address{"127.0.0.1"};
	std::uint16_t port{};
	std::string protocol;
	std::vector<std::uint32_t> versions;
	// The server's secret key (Key.h). A client whose pinned public key is not its own is refused as
	// KeyMismatch.
	std::string secretKey;
	std::size_t maxSessions{32};
	// Connections not yet admitted, on top of the sessions: past both, the transport ignores a new
	// one, which times out at its client without a reason.
	std::size_t maxPending{8};
	std::chrono::milliseconds handshakeTimeout{2000};
	// Messages a session may have waiting for Receive; one more closes it with InboundOverflow.
	std::size_t maxInbound{256};
	// The least time between two snapshots to one session.
	std::chrono::milliseconds snapshotInterval{100};
	Limits limits;
};

struct ServerEvent
{
	enum class Type : std::uint8_t
	{
		Admitted,
		// Its queue's messages are gone with it.
		Closed
	};

	Type type{};
	SessionId session{};
	Reason reason{Reason::Closed};
};

// The game's answer to a hello whose protocol and version the server accepts: true admits the
// session. It is called inside Poll, once per session, and refusing is BadCredential. Called only
// while a place is free, so admitting cannot exceed maxSessions.
using Verifier = std::function<bool(SessionId, std::span<const std::byte> credential)>;

// A session's view of the world, encoded: what SendSnapshots sends it. Filtering happens here, on the
// server. nullopt sends nothing, and the session is asked again at the next SendSnapshots.
using SnapshotView = std::function<std::optional<std::vector<std::byte>>(SessionId)>;

// The server end, on ENet over UDP, every message sealed to the connection's keys. Single-threaded:
// every call is from the thread that made it, and nothing happens between calls, so the game polls
// between its steps and the network never steps the world.
class Server
{
  public:
	// Throws std::invalid_argument for a secret key that is not one, std::runtime_error when the
	// address cannot be bound.
	Server(ServerConfig config, Verifier verifier);

	~Server();

	Server(const Server &) = delete;

	Server &operator=(const Server &) = delete;

	std::uint16_t Port() const;

	// Sends what is queued and takes in what has arrived, waiting up to wait for the first of it:
	// handshakes, admissions, messages into their sessions' queues, closings. Returns what happened
	// to sessions, in order. Never blocks longer than wait; zero does not block.
	std::vector<ServerEvent> Poll(std::chrono::milliseconds wait = std::chrono::milliseconds{0});

	// The oldest message waiting from an admitted session, or nullopt.
	std::optional<Message> Receive(SessionId session);

	// Queues a message for an admitted session, sent at the next Poll. False for a session that is
	// not open, a kind the engine keeps, or a packet over the limit.
	bool Send(SessionId session, Channel channel, MessageKind kind, std::span<const std::byte> body);

	// Sends each admitted session whose last snapshot is at least snapshotInterval old the view's
	// body for it, on the unreliable channel, with the step it was taken at. Logs and skips a view
	// over the limit.
	void SendSnapshots(std::uint64_t step, const SnapshotView &view);

	// Closes an admitted session, telling its client why; no Closed event follows for it.
	void Disconnect(SessionId session, Reason reason);

	std::vector<SessionId> Sessions() const;

	// Whether a session closed here is still closing: what it was sent before is not all acknowledged,
	// or its client has not answered the close. Poll until it is not, or a deadline passes, to have a
	// server's last messages delivered before it is destroyed, which closes at once.
	bool Closing() const;

  private:
	struct Impl;
	std::unique_ptr<Impl> m_pImpl;
};
} // namespace Jettison::Net
