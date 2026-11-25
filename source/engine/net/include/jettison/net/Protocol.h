#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Net
{
// Reliable is reliable-ordered: the handshake, requests and their results. Unreliable is
// unreliable-sequenced: a packet older than one already delivered on it is dropped, so the newest
// supersedes whatever is still in flight (snapshots, a real-time game's inputs).
enum class Channel : std::uint8_t
{
	Reliable,
	Unreliable
};

// Why a connection ended, as both ends report it. From UnknownProtocol to HandshakeTimeout, and
// KeyMismatch, they are the handshake's refusals. The reason travels unsealed, so one on the path
// could close a connection with any reason, though never read or forge a message.
enum class Reason : std::uint32_t
{
	// The other end closed it, or nothing was heard from it within the timeout.
	Closed,
	UnknownProtocol,
	VersionMismatch,
	BadCredential,
	ServerFull,
	HandshakeTimeout,
	// Anything but an offer and then a hello before admission, a hello that does not decode, a packet
	// that does not open after admission, or a kind the engine keeps for itself.
	BadMessage,
	InboundOverflow,
	// The game's reasons for closing an admitted session: its credential was revoked, or the server
	// is stopping.
	Revoked,
	Shutdown,
	// The client's hello was not sealed to the server's key: it pinned another server's.
	KeyMismatch
};

std::string_view ReasonName(Reason reason);

// What a hello names, and what a server answers: a client of another name is UnknownProtocol, of a
// version the server does not list VersionMismatch. A message's shape changing is a new version.
struct Protocol
{
	std::string name;
	std::uint32_t version{};
};

// A message's kind is the game's number for it. Kinds from kFirstEngineKind up are the engine's
// own (the hello, admission, snapshots), and a game's message of one is refused.
using MessageKind = std::uint16_t;

inline constexpr MessageKind kFirstEngineKind = 0xFF00;

// A message as received: its kind and its body, still encoded (in the binary archive, by the
// protocol's contract), from a packet no longer than the limit.
struct Message
{
	Channel channel{Channel::Reliable};
	MessageKind kind{};
	std::vector<std::byte> body;
};

// What bounds one end. maxMessageBytes is a whole message before it is sealed, kind and snapshot step
// included, however
// many datagrams the transport splits it into: a longer one is dropped by the transport before it is
// reassembled, let alone decoded, and Send refuses to send one. An unreliable message's fragments are
// sent at once and all must arrive, so it must fit the receiver's socket buffer too, the 256 KiB ENet
// asks for: on loopback 256 KiB arrived every time and 384 KiB never (2026-11-24). A peer not heard
// from for longer than timeout is closed.
struct Limits
{
	std::size_t maxMessageBytes{std::size_t{64} * 1024};
	std::chrono::milliseconds timeout{5000};
};
} // namespace Jettison::Net
