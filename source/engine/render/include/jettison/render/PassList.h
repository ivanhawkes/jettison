#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Render
{
// The frame as a declared list of passes (docs/architecture.md, "The frame"). Each pass names the
// resources it touches and how; Compile turns that into the barriers between passes once, at
// start-up, so a pass adds its uses rather than its synchronisation, and compute passes sit
// beside graphics ones on the same terms. Nothing here names Vulkan: the backend reads each
// Usage's stage, access and layout from its own table (vulkan/PassBarriers) and binds each
// resource to an image or a buffer every frame.
//
// Every resource is per frame: its contents are not wanted at the start of each frame, which is
// what the opaque pass's clears and the swapchain's acquire make true. The first use in a frame
// still waits on the resource's last use in the frame before (Barrier::discard), which the plan
// knows because every frame runs the same plan: a target that one frame samples and the next
// clears must not be cleared under the read. A buffer has no layout to lose, so its contents do
// survive: a pass whose output would be what is already there may skip its work (the cluster
// bounds), and its barriers are recorded all the same.
enum class PassKind
{
	Graphics,
	Compute,
	// Copies and fills, outside any shader.
	Transfer
};

enum class ResourceKind
{
	Image,
	Buffer
};

// How a pass touches a resource.
enum class Usage
{
	ColorAttachment,   // rendered or resolved into
	DepthAttachment,   // depth tested and written, or resolved into
	StencilAttachment, // stencil tested and written, or resolved into
	// Stencil tested and not written, sampled, or both, in the one layout that allows each. The
	// depth target's depth and its stencil are resources of their own, so each has its own layout.
	StencilRead,
	SampledFragment,     // an image read through a sampler in a fragment shader
	SampledCompute,      // ... in a compute shader
	StorageReadVertex,   // a storage buffer read by a vertex shader
	StorageReadFragment, // ... by a fragment shader
	StorageReadCompute,  // a storage buffer or image read by a compute shader
	StorageWriteCompute, // ... written by one
	TransferRead,        // copied from
	TransferWrite,       // copied or filled into
	Present,             // handed to the presentation engine; only as a resource's final use
};

std::string_view ToString(Usage usage);
bool IsWrite(Usage usage);

struct PassResource
{
	std::uint32_t index{0};
	friend bool operator==(const PassResource &, const PassResource &) = default;
};

struct PassUse
{
	PassResource resource;
	Usage usage{Usage::SampledFragment};
};

// What has to have finished before a pass may use a resource as `after`: the uses since the last
// write, or the last write itself. Discard means the contents are not wanted (the first use in a
// frame), so an image's layout may start from undefined; `before` is then the resource's last
// use in the previous frame.
struct Barrier
{
	PassResource resource;
	std::vector<Usage> before;
	Usage after{Usage::SampledFragment};
	bool discard{false};
};

struct PlannedPass
{
	std::uint32_t pass{0};
	std::vector<Barrier> barriers;
};

struct PassPlan
{
	// In the order the passes were added, each with the barriers recorded before it.
	std::vector<PlannedPass> passes;
	// After the last pass: what leaves each exported resource in its final use.
	std::vector<Barrier> final;
};

class PassList
{
  public:
	PassResource AddImage(std::string name);
	PassResource AddBuffer(std::string name);
	// The resources are this list's, and each use is legal for its resource's kind and the pass's
	// kind; anything else throws std::logic_error, naming the pass and the resource.
	std::uint32_t AddPass(std::string name, PassKind kind, std::vector<PassUse> uses);
	// The use a resource is left in after the last pass: the swapchain image, presented.
	void Export(PassResource resource, Usage final);

	// Throws std::logic_error for a resource read before any pass writes it, which would read the
	// discarded contents, and for a pass that names one resource twice.
	PassPlan Compile() const;

	std::string_view PassName(std::uint32_t pass) const;
	PassKind KindOf(std::uint32_t pass) const;
	std::string_view ResourceName(PassResource resource) const;
	ResourceKind KindOf(PassResource resource) const;
	std::size_t PassCount() const
	{
		return m_passes.size();
	}
	std::size_t ResourceCount() const
	{
		return m_resources.size();
	}

  private:
	struct Resource
	{
		std::string name;
		ResourceKind kind{ResourceKind::Image};
		bool exported{false};
		Usage final{Usage::Present};
	};
	struct Pass
	{
		std::string name;
		PassKind kind{PassKind::Graphics};
		std::vector<PassUse> uses;
	};

	std::vector<Resource> m_resources;
	std::vector<Pass> m_passes;
};
} // namespace Jettison::Render
