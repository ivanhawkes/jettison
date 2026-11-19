#pragma once

#include <enet/enet.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <jettison/net/Protocol.h>

namespace Jettison::Net::Wire
{
// What goes on the wire, and is the protocol's: a packet is its kind, two bytes little-endian, then
// its body. A snapshot's body begins with its step, eight bytes little-endian. Changing either, or a
// kind's number, breaks every client of the build before.
enum class EngineKind : MessageKind
{
	Hello = kFirstEngineKind,
	Welcome,
	Snapshot
};

inline constexpr std::size_t kKindBytes = 2;
inline constexpr std::size_t kStepBytes = 8;
inline constexpr std::size_t kChannelCount = 2;

// Throws std::runtime_error when ENet cannot start; once per process, from any end's constructor.
void StartEnet();

// Null when the packet would be longer than limit; otherwise owned by the caller until sent.
ENetPacket *MakePacket(
    Channel channel, MessageKind kind, std::span<const std::byte> body, std::size_t limit,
    std::optional<std::uint64_t> step = std::nullopt);

// Sends the packet, destroying it if the peer will not take it. False then.
bool SendPacket(ENetPeer *pPeer, Channel channel, ENetPacket *pPacket);

std::optional<Channel> ChannelOf(std::uint8_t id);

struct Received
{
	MessageKind kind{};
	std::span<const std::byte> body;
};

// Nullopt for a packet shorter than its kind.
std::optional<Received> ReadPacket(const ENetPacket &packet);

std::optional<std::uint64_t> ReadStep(std::span<const std::byte> &body);

struct Hello
{
	std::string protocol;
	std::uint32_t version{};
	std::vector<std::byte> credential;
};

std::vector<std::byte> EncodeHello(const Hello &hello);

// Nullopt for a body that is not a hello.
std::optional<Hello> DecodeHello(std::span<const std::byte> body);
} // namespace Jettison::Net::Wire
