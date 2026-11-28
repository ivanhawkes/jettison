#include <enet/enet.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "Fuzz.h"
#include "Wire.h"

using namespace Jettison;

namespace
{
struct PacketDeleter
{
	void operator()(ENetPacket *pPacket) const
	{
		enet_packet_destroy(pPacket);
	}
};
} // namespace


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	Net::Wire::StartEnet();
	return 0;
}


// A peer's bytes as each end reads them: an unsealed packet as an offer, and the plaintext of a
// sealed one, what the AEAD layer opened, as a message, its body as a hello and as a snapshot's step.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	const std::unique_ptr<ENetPacket, PacketDeleter> pPacket{
	    enet_packet_create(pData, size, ENET_PACKET_FLAG_RELIABLE)};
	if (pPacket != nullptr)
		Net::Wire::ReadOffer(*pPacket);
	const std::optional<Net::Wire::Received> received =
	    Net::Wire::ReadMessage(Fuzz::Bytes(pData, size));
	if (!received)
		return 0;
	Net::Wire::DecodeHello(received->body);
	std::span<const std::byte> body = received->body;
	Net::Wire::ReadStep(body);
	return 0;
}
