#include "Wire.h"

#include <cstring>
#include <stdexcept>

#include <jettison/serialise/Binary.h>

namespace Jettison::Net
{
std::string_view ReasonName(Reason reason)
{
	switch (reason)
	{
		case Reason::Closed: return "closed";
		case Reason::UnknownProtocol: return "unknown protocol";
		case Reason::VersionMismatch: return "version mismatch";
		case Reason::BadCredential: return "bad credential";
		case Reason::ServerFull: return "server full";
		case Reason::HandshakeTimeout: return "handshake timeout";
		case Reason::BadMessage: return "bad message";
		case Reason::InboundOverflow: return "inbound overflow";
		case Reason::Revoked: return "revoked";
		case Reason::Shutdown: return "shutdown";
	}
	return "unknown reason";
}
} // namespace Jettison::Net

namespace Jettison::Net::Wire
{
namespace
{
constexpr std::string_view kHelloFormat = "jettison.hello";
constexpr Serialise::FormatVersion kHelloVersion{1};


void PutLittleEndian(std::byte *pOut, std::uint64_t value, std::size_t width)
{
	for (std::size_t index = 0; index < width; ++index)
		pOut[index] = static_cast<std::byte>(value >> (8 * index));
}


std::uint64_t GetLittleEndian(const std::byte *pIn, std::size_t width)
{
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < width; ++index)
		value |= static_cast<std::uint64_t>(pIn[index]) << (8 * index);
	return value;
}
} // namespace


void StartEnet()
{
	// ENet's deinitialise does nothing on Unix, so it is never called: ends outlive no static.
	static const bool started = []
	{
		if (enet_initialize() != 0)
			throw std::runtime_error("net: ENet did not initialise");
		return true;
	}();
	(void)started;
}


ENetPacket *MakePacket(
    Channel channel, MessageKind kind, std::span<const std::byte> body, std::size_t limit,
    std::optional<std::uint64_t> step)
{
	const std::size_t header = kKindBytes + (step ? kStepBytes : 0);
	if (body.size() > limit || header > limit - body.size())
		return nullptr;
	// Unreliable fragments, so a snapshot longer than the MTU is dropped whole rather than sent
	// reliably, which would hold back the newer ones behind it.
	const enet_uint32 flags =
	    channel == Channel::Reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
	ENetPacket *pPacket = enet_packet_create(nullptr, header + body.size(), flags);
	if (pPacket == nullptr)
		throw std::bad_alloc();
	auto *pData = reinterpret_cast<std::byte *>(pPacket->data);
	PutLittleEndian(pData, kind, kKindBytes);
	if (step)
		PutLittleEndian(pData + kKindBytes, *step, kStepBytes);
	if (!body.empty())
		std::memcpy(pData + header, body.data(), body.size());
	return pPacket;
}


bool SendPacket(ENetPeer *pPeer, Channel channel, ENetPacket *pPacket)
{
	if (enet_peer_send(pPeer, static_cast<enet_uint8>(channel), pPacket) == 0)
		return true;
	enet_packet_destroy(pPacket);
	return false;
}


std::optional<Channel> ChannelOf(std::uint8_t id)
{
	if (id >= kChannelCount)
		return std::nullopt;
	return static_cast<Channel>(id);
}


std::optional<Received> ReadPacket(const ENetPacket &packet)
{
	if (packet.dataLength < kKindBytes)
		return std::nullopt;
	const auto *pData = reinterpret_cast<const std::byte *>(packet.data);
	return Received{
	    static_cast<MessageKind>(GetLittleEndian(pData, kKindBytes)),
	    {pData + kKindBytes, packet.dataLength - kKindBytes}};
}


std::optional<std::uint64_t> ReadStep(std::span<const std::byte> &body)
{
	if (body.size() < kStepBytes)
		return std::nullopt;
	const std::uint64_t step = GetLittleEndian(body.data(), kStepBytes);
	body = body.subspan(kStepBytes);
	return step;
}


std::vector<std::byte> EncodeHello(const Hello &hello)
{
	Serialise::BinaryWriter writer{kHelloFormat};
	writer.WriteName(hello.protocol);
	writer.WriteUnsigned(hello.version, 4);
	writer.WriteBytes(hello.credential);
	return writer.Finish(kHelloVersion);
}


std::optional<Hello> DecodeHello(std::span<const std::byte> body)
{
	try
	{
		Serialise::BinaryReader reader{body, kHelloFormat, kHelloVersion};
		Hello hello;
		hello.protocol = reader.ReadName();
		hello.version = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
		hello.credential = reader.ReadBytes();
		reader.End();
		return hello;
	}
	catch (const std::runtime_error &)
	{
		// A peer's bytes: whatever the reader refuses is a bad hello, never the server's failure.
		return std::nullopt;
	}
}
} // namespace Jettison::Net::Wire
