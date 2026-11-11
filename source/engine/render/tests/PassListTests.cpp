#include <jettison/render/PassList.h>

#include <doctest/doctest.h>

#include <stdexcept>
#include <vector>

#include "vulkan/PassBarriers.h"

using namespace Jettison::Render;

namespace
{
// The renderer's frame: the opaque pass renders into the multisampled target and resolves into
// the HDR one, the output pass reads that into the swapchain, which is presented.
struct Frame
{
	PassList list;
	PassResource msaa;
	PassResource depth;
	PassResource hdr;
	PassResource swapchain;
	std::uint32_t opaque{};
	std::uint32_t output{};

	Frame()
	{
		msaa = list.AddImage("hdr-msaa");
		depth = list.AddImage("depth");
		hdr = list.AddImage("hdr");
		swapchain = list.AddImage("swapchain");
		opaque = list.AddPass(
		    "opaque", PassKind::Graphics,
		    {{msaa, Usage::ColorAttachment},
		     {depth, Usage::DepthAttachment},
		     {hdr, Usage::ColorAttachment}});
		output = list.AddPass(
		    "output", PassKind::Graphics,
		    {{hdr, Usage::SampledFragment}, {swapchain, Usage::ColorAttachment}});
		list.Export(swapchain, Usage::Present);
	}
};

// Stand-in handles: Translate copies them into its barriers and never dereferences them.
int g_handles[4];
template <typename Handle> Handle Fake(int index)
{
	return reinterpret_cast<Handle>(&g_handles[index]);
}

const Barrier *Find(const std::vector<Barrier> &barriers, PassResource resource)
{
	for (const Barrier &barrier : barriers)
		if (barrier.resource == resource)
			return &barrier;
	return nullptr;
}
} // namespace

TEST_CASE("Pass list: the frame's first uses discard, and wait on the last frame's last use")
{
	const Frame frame;
	const PassPlan plan = frame.list.Compile();
	REQUIRE(plan.passes.size() == 2);
	const auto &opaque = plan.passes[0].barriers;
	REQUIRE(opaque.size() == 3);

	// Last frame resolved into the HDR target and its output pass sampled it: this frame's
	// resolve waits on that read (and the write before it) before it writes, and its contents
	// are not wanted.
	const Barrier *hdr = Find(opaque, frame.hdr);
	REQUIRE(hdr != nullptr);
	CHECK(hdr->discard);
	CHECK(hdr->before == std::vector<Usage>{Usage::ColorAttachment, Usage::SampledFragment});
	CHECK(hdr->after == Usage::ColorAttachment);

	// The depth target was last written by last frame's opaque pass: write after write.
	const Barrier *depth = Find(opaque, frame.depth);
	REQUIRE(depth != nullptr);
	CHECK(depth->before == std::vector<Usage>{Usage::DepthAttachment});
}

TEST_CASE("Pass list: a target written then sampled gets one barrier, and the swapchain is presented")
{
	const Frame frame;
	const PassPlan plan = frame.list.Compile();
	const auto &output = plan.passes[1].barriers;
	REQUIRE(output.size() == 2);
	const Barrier *hdr = Find(output, frame.hdr);
	REQUIRE(hdr != nullptr);
	CHECK_FALSE(hdr->discard);
	CHECK(hdr->before == std::vector<Usage>{Usage::ColorAttachment});
	CHECK(hdr->after == Usage::SampledFragment);

	const Barrier *swapchain = Find(output, frame.swapchain);
	REQUIRE(swapchain != nullptr);
	CHECK(swapchain->discard);
	CHECK(swapchain->before == std::vector<Usage>{Usage::Present});

	REQUIRE(plan.final.size() == 1);
	CHECK(plan.final[0].resource == frame.swapchain);
	CHECK(plan.final[0].before == std::vector<Usage>{Usage::ColorAttachment});
	CHECK(plan.final[0].after == Usage::Present);
}

TEST_CASE("Pass list: compute passes sit beside graphics ones on the same terms")
{
	PassList list;
	const PassResource clusters = list.AddBuffer("clusters");
	const PassResource lights = list.AddBuffer("light-indices");
	const PassResource color = list.AddImage("color");
	list.AddPass("bounds", PassKind::Compute, {{clusters, Usage::StorageWriteCompute}});
	list.AddPass(
	    "assign", PassKind::Compute,
	    {{clusters, Usage::StorageReadCompute}, {lights, Usage::StorageWriteCompute}});
	list.AddPass(
	    "shade", PassKind::Graphics,
	    {{lights, Usage::StorageReadFragment}, {color, Usage::ColorAttachment}});
	const PassPlan plan = list.Compile();

	const Barrier *read = Find(plan.passes[1].barriers, clusters);
	REQUIRE(read != nullptr);
	CHECK(read->before == std::vector<Usage>{Usage::StorageWriteCompute});
	CHECK(read->after == Usage::StorageReadCompute);

	const Barrier *shade = Find(plan.passes[2].barriers, lights);
	REQUIRE(shade != nullptr);
	CHECK(shade->before == std::vector<Usage>{Usage::StorageWriteCompute});
	CHECK(shade->after == Usage::StorageReadFragment);

	// Compute to fragment, in Vulkan's terms: the write made available, the read made visible.
	const std::vector<BoundResource> bound{
	    {VK_NULL_HANDLE, 0, Fake<VkBuffer>(1)},
	    {VK_NULL_HANDLE, 0, Fake<VkBuffer>(2)},
	    {Fake<VkImage>(3), VK_IMAGE_ASPECT_COLOR_BIT, VK_NULL_HANDLE}};
	const VulkanBarriers translated = Translate(plan.passes[2].barriers, bound);
	REQUIRE(translated.buffers.size() == 1);
	CHECK(translated.buffers[0].srcStageMask == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
	CHECK(translated.buffers[0].srcAccessMask == VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	CHECK(translated.buffers[0].dstStageMask == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
	CHECK(translated.buffers[0].dstAccessMask == VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
	REQUIRE(translated.images.size() == 1);
	CHECK(translated.images[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
	CHECK(translated.images[0].newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

TEST_CASE("Pass list: transfer passes fill before compute and copy out after the fragments read")
{
	// The renderer's cluster frame: the index list's allocator is zeroed, the assignment writes the
	// list, the opaque pass reads it, and the check copies it out.
	PassList list;
	const PassResource indices = list.AddBuffer("light-indices");
	const PassResource color = list.AddImage("color");
	list.AddPass("reset", PassKind::Transfer, {{indices, Usage::TransferWrite}});
	list.AddPass("assign", PassKind::Compute, {{indices, Usage::StorageWriteCompute}});
	list.AddPass(
	    "shade", PassKind::Graphics,
	    {{indices, Usage::StorageReadFragment}, {color, Usage::ColorAttachment}});
	list.AddPass("readback", PassKind::Transfer, {{indices, Usage::TransferRead}});
	const PassPlan plan = list.Compile();

	// The next frame's fill waits on this frame's fragment reads and on the copy.
	const Barrier *reset = Find(plan.passes[0].barriers, indices);
	REQUIRE(reset != nullptr);
	CHECK(reset->discard);
	CHECK(
	    reset->before ==
	    std::vector<Usage>{
	        Usage::StorageWriteCompute, Usage::StorageReadFragment, Usage::TransferRead});
	// The copy waits on the compute write, which the fragment read before it did not make
	// visible to transfers.
	const Barrier *readback = Find(plan.passes[3].barriers, indices);
	REQUIRE(readback != nullptr);
	CHECK(
	    readback->before == std::vector<Usage>{Usage::StorageWriteCompute, Usage::StorageReadFragment});

	const std::vector<BoundResource> bound{
	    {VK_NULL_HANDLE, 0, Fake<VkBuffer>(1)},
	    {Fake<VkImage>(2), VK_IMAGE_ASPECT_COLOR_BIT, VK_NULL_HANDLE}};
	const VulkanBarriers assign = Translate(plan.passes[1].barriers, bound);
	REQUIRE(assign.buffers.size() == 1);
	CHECK(assign.buffers[0].srcStageMask == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
	CHECK(assign.buffers[0].srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
	CHECK(assign.buffers[0].dstStageMask == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
	const VulkanBarriers copy = Translate(plan.passes[3].barriers, bound);
	REQUIRE(copy.buffers.size() == 1);
	CHECK(copy.buffers[0].dstStageMask == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
	CHECK(copy.buffers[0].dstAccessMask == VK_ACCESS_2_TRANSFER_READ_BIT);

	CHECK_THROWS_WITH_AS(
	    list.AddPass("p", PassKind::Compute, {{indices, Usage::TransferRead}}),
	    "Pass 'p' uses 'light-indices' as TransferRead, which is not a use in a compute pass",
	    std::logic_error);
	CHECK_THROWS_WITH_AS(
	    list.AddPass("p", PassKind::Transfer, {{indices, Usage::StorageReadCompute}}),
	    "Pass 'p' uses 'light-indices' as StorageReadCompute, which is not a use in a transfer pass",
	    std::logic_error);
}

TEST_CASE(
    "Pass list: a second read at a stage already waited for needs nothing; a write waits on every read")
{
	PassList list;
	const PassResource image = list.AddImage("image");
	const PassResource out = list.AddImage("out");
	list.AddPass("write", PassKind::Graphics, {{image, Usage::ColorAttachment}});
	list.AddPass("read-a", PassKind::Graphics, {{image, Usage::SampledFragment}});
	list.AddPass(
	    "read-b", PassKind::Graphics, {{image, Usage::SampledFragment}, {out, Usage::ColorAttachment}});
	list.AddPass("read-c", PassKind::Compute, {{image, Usage::SampledCompute}});
	list.AddPass("rewrite", PassKind::Graphics, {{image, Usage::ColorAttachment}});
	const PassPlan plan = list.Compile();

	CHECK(Find(plan.passes[2].barriers, image) == nullptr);
	const Barrier *compute = Find(plan.passes[3].barriers, image);
	REQUIRE(compute != nullptr);
	CHECK(compute->before == std::vector<Usage>{Usage::ColorAttachment, Usage::SampledFragment});
	const Barrier *rewrite = Find(plan.passes[4].barriers, image);
	REQUIRE(rewrite != nullptr);
	CHECK(
	    rewrite->before ==
	    std::vector<Usage>{Usage::ColorAttachment, Usage::SampledFragment, Usage::SampledCompute});

	// The image is in the shader-read layout when the rewrite's barrier moves it.
	const std::vector<BoundResource> bound{
	    {Fake<VkImage>(1), VK_IMAGE_ASPECT_COLOR_BIT, VK_NULL_HANDLE},
	    {Fake<VkImage>(2), VK_IMAGE_ASPECT_COLOR_BIT, VK_NULL_HANDLE}};
	const VulkanBarriers translated = Translate(plan.passes[4].barriers, bound);
	REQUIRE(translated.images.size() == 1);
	CHECK(translated.images[0].oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(
	    translated.images[0].srcStageMask ==
	    (VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
	     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
	CHECK(translated.images[0].srcAccessMask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

TEST_CASE("Pass list: a read in a layout the image has left is waited for again")
{
	PassList list;
	const PassResource image = list.AddImage("image");
	list.AddPass("write", PassKind::Graphics, {{image, Usage::ColorAttachment}});
	list.AddPass("sample", PassKind::Graphics, {{image, Usage::SampledFragment}});
	list.AddPass("storage", PassKind::Compute, {{image, Usage::StorageReadCompute}});
	list.AddPass("sample-again", PassKind::Graphics, {{image, Usage::SampledFragment}});
	const PassPlan plan = list.Compile();
	CHECK(Find(plan.passes[3].barriers, image) != nullptr);
}

TEST_CASE("Pass list: what cannot be planned is refused, naming the pass")
{
	PassList list;
	const PassResource image = list.AddImage("image");
	const PassResource buffer = list.AddBuffer("buffer");
	CHECK_THROWS_WITH_AS(
	    list.AddPass("p", PassKind::Graphics, {{buffer, Usage::ColorAttachment}}),
	    "Pass 'p' uses 'buffer' as ColorAttachment, which is not a use of a buffer", std::logic_error);
	CHECK_THROWS_WITH_AS(
	    list.AddPass("p", PassKind::Compute, {{image, Usage::SampledFragment}}),
	    "Pass 'p' uses 'image' as SampledFragment, which is not a use in a compute pass",
	    std::logic_error);
	CHECK_THROWS_WITH_AS(
	    list.AddPass(
	        "p", PassKind::Graphics,
	        {{image, Usage::ColorAttachment}, {image, Usage::SampledFragment}}),
	    "Pass 'p' names 'image' twice", std::logic_error);
	CHECK_THROWS_AS(
	    list.AddPass("p", PassKind::Graphics, {{{7}, Usage::ColorAttachment}}), std::logic_error);

	list.AddPass("reader", PassKind::Graphics, {{image, Usage::SampledFragment}});
	CHECK_THROWS_WITH_AS(
	    list.Compile(), "Pass 'reader' reads 'image' before any pass writes it this frame",
	    std::logic_error);
}
