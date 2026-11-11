#pragma once

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Physics/StateRecorder.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Jettison::Physics::Detail
{
// Jolt's process-wide set-up, once, before the first physics world: its allocator, the factory and
// the types it creates shapes through, and its trace and assertion hooks routed to the engine's log
// and assertions. Undone at exit. Every physics world calls it.
void EnsureJolt();

// A Jolt state recorder over a byte vector, so the state goes into a save as a value and comes back
// out of one without a stream between. Reading past the end fails the recorder rather than reading
// what is not there, which Jolt's own stringstream recorder would leave undefined.
class ByteRecorder final : public JPH::StateRecorder
{
  public:
	ByteRecorder() = default;
	explicit ByteRecorder(std::vector<std::uint8_t> bytes) : m_bytes{std::move(bytes)}
	{
	}

	void WriteBytes(const void *data, std::size_t size) override;
	void ReadBytes(void *data, std::size_t size) override;
	bool IsEOF() const override
	{
		return m_read == m_bytes.size();
	}
	bool IsFailed() const override
	{
		return m_failed;
	}

	const std::vector<std::uint8_t> &Bytes() const
	{
		return m_bytes;
	}

  private:
	std::vector<std::uint8_t> m_bytes;
	std::size_t m_read{0};
	bool m_failed{false};
};
} // namespace Jettison::Physics::Detail
