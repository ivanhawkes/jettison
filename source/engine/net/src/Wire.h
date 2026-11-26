#pragma once

#include <enet/enet.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <jettison/net/Protocol.h>

#include "Seal.h"

namespace Jettison::Net::Wire
{
// What goes on the wire, and is the protocol's: a message is its kind, two bytes little-endian, then
// its body, sealed (Seal.h). A snapshot's body begins with its step, eight bytes little-endian. Only
// the offers, each end's ephemeral key, go unsealed: the client's first, the server's in answer with
// its sealed proof, then the client's sealed hello and the server's sealed welcome. Changing any of it,
// or a kind's number, breaks every client of the build before.
enum class EngineKind : MessageKind
{
	Hello = kFirstEngineKind,
	Welcome,
	Snapshot,
	Offer
};

inline constexpr std::size_t kKindBytes = 2;
inline constexpr std::size_t kStepBytes = 8;
inline constexpr std::size_t kChannelCount = Seal::kChannelCount;

// Throws std::runtime_error when ENet cannot start; once per process, from any end's constructor.
void StartEnet();

// Sealed. Null when the message would be longer than limit, or the keys have sealed all they may;
// otherwise owned by the caller until sent.
ENetPacket *MakePacket(
    Seal::Keys &keys, Channel channel, MessageKind kind, std::span<const std::byte> body,
    std::size_t limit, std::optional<std::uint64_t> step = std::nullopt);

// Reliable and unsealed: the client's ephemeral key. Owned by the caller until sent.
ENetPacket *MakeOffer(const Seal::Key &key);

// The server's ephemeral key, then its proof: a message of the offer's kind and no body, sealed with
// the connection's keys, which only the holder of the pinned secret can make. The client sends its
// credential only once the proof opens, so an offer of any other key gets none. Null when the keys
// have sealed all they may.
ENetPacket *MakeOffer(const Seal::Key &key, Seal::Keys &keys);

struct Offer
{
	Seal::Key key{};

	// Empty in the client's offer. A view of the packet, which it must not outlive.
	std::span<const std::byte> proof;
};

// Nullopt for a packet that is not an offer.
std::optional<Offer> ReadOffer(const ENetPacket &packet);

// The packet's bytes opened, or nullopt (Seal::Keys::Open).
std::optional<std::vector<std::byte>> Open(Seal::Keys &keys, Channel channel, const ENetPacket &packet);

// Sends the packet, destroying it if the peer will not take it. False then.
bool SendPacket(ENetPeer *pPeer, Channel channel, ENetPacket *pPacket);

std::optional<Channel> ChannelOf(std::uint8_t id);

struct Received
{
	MessageKind kind{};
	std::span<const std::byte> body;
};

// Reads an opened message, which it must outlive. Nullopt for one shorter than its kind.
std::optional<Received> ReadMessage(std::span<const std::byte> message);

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
