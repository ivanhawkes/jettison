#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <jettison/render/FrameStats.h>
#include <jettison/render/vulkan/DeviceContext.h>

namespace Jettison::Render
{
// The GPU's time per declared pass (docs/architecture.md, "The frame"), two ways. A timestamp before
// and after each pass, in a query pool of each frame in flight, read back once the frame's fence has
// been waited on, so the engine can report its own budget with no profiler attached; and, when Tracy
// is compiled in (JETTISON_PROFILE), a Tracy GPU zone over the same commands, named after the pass,
// so the passes show on the profiler's timeline under the CPU zones that recorded them.
//
// A timestamp is written once every earlier command has finished (the all-commands stage), so a
// pass's span runs from the end of the work before it to the end of its own. The barriers between
// passes serialise them, so the spans add up to the frame's.
class GpuProfiler
{
  public:
	GpuProfiler(
	    std::shared_ptr<DeviceContext> pDeviceContext, std::uint32_t framesInFlight,
	    std::vector<std::string> passNames);
	~GpuProfiler();

	GpuProfiler(const GpuProfiler &) = delete;
	GpuProfiler &operator=(const GpuProfiler &) = delete;

	// A device whose graphics queue has no timestamps measures nothing, and says so once.
	void Init();
	void Destroy();

	// After the slot's fence: what the slot's last frame measured joins the timings.
	void Collect(std::uint32_t slot);
	// At the head of the slot's command buffer, outside any rendering: resets its queries, and hands
	// Tracy's the commands it needs to read its own.
	void BeginFrame(VkCommandBuffer commandBuffer, std::uint32_t slot);
	void BeginPass(VkCommandBuffer commandBuffer, std::uint32_t slot, std::uint32_t pass);
	void EndPass(VkCommandBuffer commandBuffer, std::uint32_t slot, std::uint32_t pass);
	// Tracy's GPU context, for the zones the renderer opens around each pass; null when Tracy is
	// compiled out, when its macros expand to nothing.
	void *TracyContext() const
	{
		return m_pTracyContext;
	}

	bool Measures() const
	{
		return m_pool != VK_NULL_HANDLE;
	}
	// One per pass, in the pass list's order, then the frame's total, from the first pass's start to
	// the last one's end.
	const std::vector<PassTiming> &Timings() const
	{
		return m_timings;
	}
	std::uint64_t MeasuredFrames() const
	{
		return m_measuredFrames;
	}
	// Starts the means again: a benchmark's warm-up is not part of what it measures.
	void ResetMeans();

  private:
	std::shared_ptr<DeviceContext> m_pDeviceContext;
	std::uint32_t m_framesInFlight{0};
	std::vector<std::string> m_passNames;
	VkQueryPool m_pool{VK_NULL_HANDLE};
	// Nanoseconds a timestamp tick is, and the bits of it that count.
	double m_period{1.0};
	std::uint64_t m_validMask{~std::uint64_t{0}};
	std::vector<bool> m_slotRecorded;
	std::vector<PassTiming> m_timings;
	std::vector<double> m_sums;
	std::uint64_t m_measuredFrames{0};
	void *m_pTracyContext{nullptr};
};
} // namespace Jettison::Render
