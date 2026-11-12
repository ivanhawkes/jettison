#include <jettison/render/PassList.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Jettison::Render
{
namespace
{
// An image's layout follows from how it is used; two uses that share one need no transition
// between them. Buffers have no layout.
enum class Layout
{
	Color,
	Depth,
	Stencil,
	StencilRead,
	ShaderRead,
	General,
	TransferSource,
	TransferDestination,
	Present,
	None
};

Layout LayoutOf(Usage usage)
{
	switch (usage)
	{
		case Usage::ColorAttachment: return Layout::Color;
		case Usage::DepthAttachment: return Layout::Depth;
		case Usage::StencilAttachment: return Layout::Stencil;
		case Usage::StencilRead: return Layout::StencilRead;
		case Usage::SampledFragment:
		case Usage::SampledCompute: return Layout::ShaderRead;
		case Usage::StorageReadCompute:
		case Usage::StorageWriteCompute: return Layout::General;
		case Usage::TransferRead: return Layout::TransferSource;
		case Usage::TransferWrite: return Layout::TransferDestination;
		case Usage::Present: return Layout::Present;
		case Usage::StorageReadVertex:
		case Usage::StorageReadFragment: return Layout::None;
	}
	return Layout::None;
}

bool ValidFor(Usage usage, ResourceKind kind)
{
	switch (usage)
	{
		case Usage::ColorAttachment:
		case Usage::DepthAttachment:
		case Usage::StencilAttachment:
		case Usage::StencilRead:
		case Usage::SampledFragment:
		case Usage::SampledCompute:
		case Usage::Present: return kind == ResourceKind::Image;
		case Usage::StorageReadVertex:
		case Usage::StorageReadFragment: return kind == ResourceKind::Buffer;
		case Usage::StorageReadCompute:
		case Usage::StorageWriteCompute:
		case Usage::TransferRead:
		case Usage::TransferWrite: return true;
	}
	return false;
}

bool ValidIn(Usage usage, PassKind kind)
{
	switch (usage)
	{
		case Usage::ColorAttachment:
		case Usage::DepthAttachment:
		case Usage::StencilAttachment:
		case Usage::StencilRead:
		case Usage::SampledFragment:
		case Usage::StorageReadVertex:
		case Usage::StorageReadFragment: return kind == PassKind::Graphics;
		case Usage::SampledCompute:
		case Usage::StorageReadCompute:
		case Usage::StorageWriteCompute: return kind == PassKind::Compute;
		case Usage::TransferRead:
		case Usage::TransferWrite: return kind == PassKind::Transfer;
		case Usage::Present: return false;
	}
	return false;
}

// The uses a resource has had since its last write, the write first. Its last entry is the use
// that set the image's current layout: a read appended without a barrier shares the layout of
// the one before it, and a read that changes the layout is appended by its barrier.
using State = std::vector<Usage>;

// Advances a resource's state by one use, and says what the use has to wait for.
std::optional<Barrier> Apply(State &state, PassResource resource, Usage use, const State &previousFrame)
{
	if (state.empty())
	{
		// The first use this frame. The contents are not wanted, but the last frame's uses of the
		// same resource still have to be done before this one overwrites or relayouts it.
		Barrier barrier{resource, previousFrame, use, true};
		state = {use};
		return barrier;
	}
	if (IsWrite(use))
	{
		Barrier barrier{resource, state, use, false};
		state = {use};
		return barrier;
	}
	// A read that has already been made visible at this stage, with the image still in its
	// layout: a read in another layout since then has moved it.
	if (std::ranges::find(state, use) != state.end() && LayoutOf(use) == LayoutOf(state.back()))
		return std::nullopt;
	Barrier barrier{resource, state, use, false};
	state.push_back(use);
	return barrier;
}
} // namespace

std::string_view ToString(Usage usage)
{
	switch (usage)
	{
		case Usage::ColorAttachment: return "ColorAttachment";
		case Usage::DepthAttachment: return "DepthAttachment";
		case Usage::StencilAttachment: return "StencilAttachment";
		case Usage::StencilRead: return "StencilRead";
		case Usage::SampledFragment: return "SampledFragment";
		case Usage::SampledCompute: return "SampledCompute";
		case Usage::StorageReadVertex: return "StorageReadVertex";
		case Usage::StorageReadFragment: return "StorageReadFragment";
		case Usage::StorageReadCompute: return "StorageReadCompute";
		case Usage::StorageWriteCompute: return "StorageWriteCompute";
		case Usage::TransferRead: return "TransferRead";
		case Usage::TransferWrite: return "TransferWrite";
		case Usage::Present: return "Present";
	}
	return "?";
}

bool IsWrite(Usage usage)
{
	// An attachment is read as well as written (depth tests, blending), and is a write for
	// ordering: nothing may touch it while a pass renders into it.
	return usage == Usage::ColorAttachment || usage == Usage::DepthAttachment ||
	       usage == Usage::StencilAttachment || usage == Usage::StorageWriteCompute ||
	       usage == Usage::TransferWrite;
}

PassResource PassList::AddImage(std::string name)
{
	m_resources.push_back({std::move(name), ResourceKind::Image});
	return {static_cast<std::uint32_t>(m_resources.size() - 1)};
}

PassResource PassList::AddBuffer(std::string name)
{
	m_resources.push_back({std::move(name), ResourceKind::Buffer});
	return {static_cast<std::uint32_t>(m_resources.size() - 1)};
}

std::uint32_t PassList::AddPass(std::string name, PassKind kind, std::vector<PassUse> uses)
{
	for (std::size_t index = 0; index < uses.size(); ++index)
	{
		const PassUse &use = uses[index];
		if (use.resource.index >= m_resources.size())
			throw std::logic_error("Pass '" + name + "' uses a resource this pass list does not have");
		const Resource &resource = m_resources[use.resource.index];
		const std::string what =
		    "Pass '" + name + "' uses '" + resource.name + "' as " + std::string{ToString(use.usage)};
		if (!ValidFor(use.usage, resource.kind))
			throw std::logic_error(
			    what + ", which is not a use of " +
			    (resource.kind == ResourceKind::Image ? "an image" : "a buffer"));
		if (!ValidIn(use.usage, kind))
			throw std::logic_error(
			    what + ", which is not a use in a " +
			    (kind == PassKind::Graphics  ? "graphics"
			     : kind == PassKind::Compute ? "compute"
			                                 : "transfer") +
			    " pass");
		for (std::size_t other = 0; other < index; ++other)
			if (uses[other].resource == use.resource)
				throw std::logic_error("Pass '" + name + "' names '" + resource.name + "' twice");
	}
	m_passes.push_back({std::move(name), kind, std::move(uses)});
	return static_cast<std::uint32_t>(m_passes.size() - 1);
}

void PassList::Export(PassResource resource, Usage final)
{
	if (resource.index >= m_resources.size())
		throw std::logic_error("Export names a resource this pass list does not have");
	Resource &exported = m_resources[resource.index];
	if (!ValidFor(final, exported.kind))
		throw std::logic_error(
		    "Export leaves '" + exported.name + "' as " + std::string{ToString(final)} +
		    ", which is not a use of its kind");
	exported.exported = true;
	exported.final = final;
}

PassPlan PassList::Compile() const
{
	// Twice through: the first walk finds each resource's state at the end of a frame, which is
	// what the second walk's first uses wait on, because every frame runs this plan.
	std::vector<State> endOfFrame(m_resources.size());
	PassPlan plan;
	for (int walk = 0; walk < 2; ++walk)
	{
		std::vector<State> states(m_resources.size());
		plan = {};
		for (std::uint32_t pass = 0; pass < m_passes.size(); ++pass)
		{
			PlannedPass planned{pass, {}};
			for (const PassUse &use : m_passes[pass].uses)
			{
				State &state = states[use.resource.index];
				if (state.empty() && !IsWrite(use.usage))
					throw std::logic_error(
					    "Pass '" + m_passes[pass].name + "' reads '" +
					    m_resources[use.resource.index].name +
					    "' before any pass writes it this frame");
				if (auto barrier =
				        Apply(state, use.resource, use.usage, endOfFrame[use.resource.index]))
					planned.barriers.push_back(std::move(*barrier));
			}
			plan.passes.push_back(std::move(planned));
		}
		for (std::uint32_t index = 0; index < m_resources.size(); ++index)
		{
			const Resource &resource = m_resources[index];
			if (!resource.exported || states[index].empty())
				continue;
			plan.final.push_back({{index}, states[index], resource.final, false});
			states[index] = {resource.final};
		}
		endOfFrame = std::move(states);
	}
	return plan;
}

std::string_view PassList::PassName(std::uint32_t pass) const
{
	return m_passes.at(pass).name;
}

PassKind PassList::KindOf(std::uint32_t pass) const
{
	return m_passes.at(pass).kind;
}

std::string_view PassList::ResourceName(PassResource resource) const
{
	return m_resources.at(resource.index).name;
}

ResourceKind PassList::KindOf(PassResource resource) const
{
	return m_resources.at(resource.index).kind;
}
} // namespace Jettison::Render
