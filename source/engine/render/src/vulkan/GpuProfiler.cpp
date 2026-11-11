#include "vulkan/GpuProfiler.h"

#include <tracy/TracyVulkan.hpp>

#include <stdexcept>
#include <string_view>
#include <utility>

#include <jettison/core/Log.h>

namespace Jettison::Render
{
namespace
{
constexpr Core::Category kRender{"render"};
}

GpuProfiler::GpuProfiler(
    std::shared_ptr<DeviceContext> pDeviceContext, std::uint32_t framesInFlight,
    std::vector<std::string> passNames)
    : m_pDeviceContext{std::move(pDeviceContext)}, m_framesInFlight{framesInFlight},
      m_passNames{std::move(passNames)}
{
}

GpuProfiler::~GpuProfiler() = default;

void GpuProfiler::Init()
{
	m_timings.clear();
	for (const std::string &name : m_passNames)
		m_timings.push_back({name});
	m_timings.push_back({"frame"});
	m_sums.assign(m_timings.size(), 0.0);
	m_slotRecorded.assign(m_framesInFlight, false);
	m_measuredFrames = 0;

	const VkPhysicalDevice physicalDevice = m_pDeviceContext->GetPhysicalDevice();
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(physicalDevice, &properties);
	std::uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
	std::vector<VkQueueFamilyProperties> families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
	const std::uint32_t validBits =
	    families.at(m_pDeviceContext->GetGraphicsQueueIndex()).timestampValidBits;
	if (validBits == 0)
	{
		Core::Log(
		    Core::Severity::Warning, kRender,
		    "the graphics queue has no timestamps: the passes' GPU times are not measured");
		return;
	}
	m_period = properties.limits.timestampPeriod;
	m_validMask = validBits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << validBits) - 1;

	VkQueryPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	poolInfo.queryCount = m_framesInFlight * 2 * static_cast<std::uint32_t>(m_passNames.size());
	if (vkCreateQueryPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
		throw std::runtime_error("failed to create the pass timestamp query pool");
	m_pDeviceContext->SetDebugName(m_pool, "pass-timestamps");

#ifdef TRACY_ENABLE
	// Tracy calibrates its context against the CPU's clock with a few submissions of its own, through
	// a command buffer it is lent for the purpose and that waits for the queue.
	VkCommandBufferAllocateInfo commandInfo{};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandInfo.commandPool = m_pDeviceContext->GetCommandPool();
	commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandInfo.commandBufferCount = 1;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	if (vkAllocateCommandBuffers(device, &commandInfo, &commandBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate Tracy's calibration command buffer");
	TracyVkCtx context =
	    TracyVkContext(physicalDevice, device, m_pDeviceContext->GetGraphicsQueue(), commandBuffer);
	vkFreeCommandBuffers(device, m_pDeviceContext->GetCommandPool(), 1, &commandBuffer);
	constexpr std::string_view kName{"graphics"};
	TracyVkContextName(context, kName.data(), static_cast<std::uint16_t>(kName.size()));
	m_pTracyContext = context;
#endif
}

void GpuProfiler::Destroy()
{
#ifdef TRACY_ENABLE
	if (m_pTracyContext != nullptr)
	{
		TracyVkDestroy(static_cast<TracyVkCtx>(m_pTracyContext));
	}
#endif
	m_pTracyContext = nullptr;
	vkDestroyQueryPool(m_pDeviceContext->GetLogicalDevice(), m_pool, nullptr);
	m_pool = VK_NULL_HANDLE;
}

void GpuProfiler::Collect(std::uint32_t slot)
{
	if (m_pool == VK_NULL_HANDLE || !m_slotRecorded[slot])
		return;
	// Once: a slot waited on again before it records another frame has nothing new.
	m_slotRecorded[slot] = false;
	const auto passes = static_cast<std::uint32_t>(m_passNames.size());
	std::vector<std::uint64_t> stamps(std::size_t{2} * passes);
	// The slot's fence has been waited on, so every query its frame wrote is available.
	const VkResult result = vkGetQueryPoolResults(
	    m_pDeviceContext->GetLogicalDevice(), m_pool, slot * 2 * passes, 2 * passes,
	    stamps.size() * sizeof(std::uint64_t), stamps.data(), sizeof(std::uint64_t),
	    VK_QUERY_RESULT_64_BIT);
	if (result != VK_SUCCESS)
		return;
	const auto milliseconds = [this](std::uint64_t from, std::uint64_t to)
	{ return static_cast<double>((to - from) & m_validMask) * m_period * 1e-6; };
	for (std::uint32_t pass = 0; pass < passes; ++pass)
		m_timings[pass].lastMs =
		    milliseconds(stamps[std::size_t{2} * pass], stamps[std::size_t{2} * pass + 1]);
	m_timings.back().lastMs = milliseconds(stamps.front(), stamps.back());
	++m_measuredFrames;
	for (std::size_t index = 0; index < m_timings.size(); ++index)
	{
		m_sums[index] += m_timings[index].lastMs;
		m_timings[index].meanMs = m_sums[index] / static_cast<double>(m_measuredFrames);
	}
}

void GpuProfiler::BeginFrame(VkCommandBuffer commandBuffer, std::uint32_t slot)
{
	if (m_pool != VK_NULL_HANDLE)
	{
		const auto passes = static_cast<std::uint32_t>(m_passNames.size());
		vkCmdResetQueryPool(commandBuffer, m_pool, slot * 2 * passes, 2 * passes);
		m_slotRecorded[slot] = true;
	}
#ifdef TRACY_ENABLE
	if (m_pTracyContext != nullptr)
	{
		TracyVkCollect(static_cast<TracyVkCtx>(m_pTracyContext), commandBuffer);
	}
#endif
}

void GpuProfiler::BeginPass(VkCommandBuffer commandBuffer, std::uint32_t slot, std::uint32_t pass)
{
	if (m_pool == VK_NULL_HANDLE)
		return;
	const auto passes = static_cast<std::uint32_t>(m_passNames.size());
	vkCmdWriteTimestamp2(
	    commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_pool, slot * 2 * passes + 2 * pass);
}

void GpuProfiler::EndPass(VkCommandBuffer commandBuffer, std::uint32_t slot, std::uint32_t pass)
{
	if (m_pool == VK_NULL_HANDLE)
		return;
	const auto passes = static_cast<std::uint32_t>(m_passNames.size());
	vkCmdWriteTimestamp2(
	    commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_pool, slot * 2 * passes + 2 * pass + 1);
}

void GpuProfiler::ResetMeans()
{
	m_sums.assign(m_timings.size(), 0.0);
	m_measuredFrames = 0;
	for (PassTiming &timing : m_timings)
		timing.meanMs = 0.0;
}
} // namespace Jettison::Render
