#include <jettison/sim/Snapshot.h>

#include <jettison/core/StringId.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>
#include <jettison/sim/SimWorld.h>

#include <algorithm>

namespace Jettison::Sim
{
namespace
{
auto Identifier(const TransformSample &sample)
{
	return entt::to_integral(sample.entity);
}
} // namespace


void RegisterTypes()
{
	// The tables are global, and a process may hold more than one world.
	static const bool registered = []
	{
		using Reflect::Attribute;
		Reflect::RegisterType<Transform>("Sim.Transform")
		    .Field<&Transform::position>("position", Attribute::Saved)
		    .Field<&Transform::orientation>("orientation", Attribute::Saved);
		Reflect::RegisterType<InFrame>("Sim.InFrame").Field<&InFrame::frame>("frame", Attribute::Saved);
		Reflect::RegisterEnum<ControllerKind>("Sim.ControllerKind")
		    .Value<ControllerKind::None>("None")
		    .Value<ControllerKind::Player>("Player")
		    .Value<ControllerKind::Ai>("Ai");
		Reflect::RegisterType<Controller>("Sim.Controller")
		    .Field<&Controller::kind>("kind", Attribute::Saved)
		    .Field<&Controller::slot>("slot", Attribute::Saved);
		Reflect::RegisterType<ControlledBy>("Sim.ControlledBy")
		    .Field<&ControlledBy::controller>("controller", Attribute::Saved);
		Reflect::RegisterType<Helm>("Sim.Helm")
		    .Field<&Helm::controller>("controller", Attribute::Saved);
		Reflect::RegisterEnum<ReasonValue>("Sim.ReasonValue")
		    .Value<ReasonValue::Integer>("Integer")
		    .Value<ReasonValue::Entity>("Entity")
		    .Value<ReasonValue::Identifier>("Identifier");
		Reflect::RegisterType<ReasonArgument>("Sim.ReasonArgument")
		    .Field<&ReasonArgument::kind>("kind", Attribute::Saved)
		    .Field<&ReasonArgument::integer>("integer", Attribute::Saved)
		    .Field<&ReasonArgument::entity>("entity", Attribute::Saved)
		    .Field<&ReasonArgument::identifier>("identifier", Attribute::Saved)
		    .Field<&ReasonArgument::definition>("definition", Attribute::Saved);
		Reflect::RegisterType<Reason>("Sim.Reason")
		    .Field<&Reason::key>("key", Attribute::Saved)
		    .Field<&Reason::arguments>("arguments", Attribute::Saved);
		return true;
	}();
	(void)registered;
}


const Serialise::Migrations<Serialise::BinaryRecord> &InFrame::Migrations()
{
	static const Serialise::Migrations<Serialise::BinaryRecord> migrations = []
	{
		Serialise::Migrations<Serialise::BinaryRecord> chain{"Sim.InFrame", {2}};
		// The world's version 4 renamed the component; its field is renamed here, as the name "system"
		// is other types' fields too.
		chain.Add(1, [](Serialise::BinaryRecord &record) { record.Rename("system", "frame"); });
		return chain;
	}();
	return migrations;
}


std::uint64_t FrameId(std::string_view frameName)
{
	return Core::StringId{frameName}.Hash();
}


const TransformSample *Snapshot::FindSample(entt::entity entity) const
{
	const auto found = std::ranges::lower_bound(transforms, entt::to_integral(entity), {}, Identifier);
	return found != transforms.end() && found->entity == entity ? &*found : nullptr;
}


const Transform *Snapshot::Find(entt::entity entity) const
{
	const TransformSample *sample = FindSample(entity);
	return sample ? &sample->transform : nullptr;
}


Snapshot Capture(const SimWorld &world)
{
	Snapshot snapshot;
	snapshot.step = world.Clock().Step();
	snapshot.time = world.Clock().Now();
	const auto view = world.Registry().view<const Transform>();
	snapshot.transforms.reserve(view.size());
	const auto &registry = world.Registry();
	for (const auto [entity, transform] : view.each())
	{
		const auto *frame = registry.try_get<InFrame>(entity);
		snapshot.transforms.push_back({entity, transform, frame ? frame->frame : kNoFrame});
	}
	std::ranges::sort(snapshot.transforms, {}, Identifier);
	for (const auto &[type, capture] : world.Shown())
		capture(registry, snapshot.shown[type]);
	return snapshot;
}


std::optional<Transform> Interpolate(
    const Snapshot &previous, const Snapshot &current, entt::entity entity, double alpha)
{
	const TransformSample *to = current.FindSample(entity);
	if (!to)
		return std::nullopt;
	const TransformSample *from = previous.FindSample(entity);
	if (!from || from->frame != to->frame)
		return to->transform;
	return Transform{
	    glm::mix(from->transform.position, to->transform.position, alpha),
	    glm::slerp(from->transform.orientation, to->transform.orientation, alpha)};
}
} // namespace Jettison::Sim
