#include <jettison/dock/Definitions.h>
#include <jettison/dock/Docks.h>

#include <jettison/ai/Types.h>
#include <jettison/physics/Components.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Dock
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		Physics::RegisterTypes();
		Sim::RegisterTypes();
		Ai::RegisterTypes();
		RegisterEnum<PadClass>("Dock.PadClass")
		    .Value<PadClass::S>("S")
		    .Value<PadClass::M>("M")
		    .Value<PadClass::L>("L")
		    .Value<PadClass::XL>("XL");
		constexpr Attribute kDefined = Attribute::Saved | Attribute::Moddable;
		RegisterType<HullDefinition>("Dock.HullDefinition", Attribute::Moddable)
		    .Field<&HullDefinition::displayName>("displayName", kDefined)
		    .Field<&HullDefinition::length>("length", kDefined)
		    .Field<&HullDefinition::beam>("beam", kDefined)
		    .Field<&HullDefinition::height>("height", kDefined)
		    .Field<&HullDefinition::pad>("pad", kDefined)
		    .Field<&HullDefinition::hatch>("hatch", kDefined);
		RegisterType<Box>("Dock.Box")
		    .Field<&Box::centre>("centre", kDefined)
		    .Field<&Box::size>("size", kDefined);
		RegisterType<PadLayout>("Dock.PadLayout")
		    .Field<&PadLayout::padClass>("class", kDefined)
		    .Field<&PadLayout::position>("position", kDefined)
		    .Field<&PadLayout::normal>("normal", kDefined)
		    .Field<&PadLayout::heading>("heading", kDefined)
		    .Field<&PadLayout::display>("display", kDefined);
		RegisterType<Bay>("Dock.Bay")
		    .Field<&Bay::position>("position", kDefined)
		    .Field<&Bay::normal>("normal", kDefined);
		RegisterType<StationLayout>("Dock.StationLayout", Attribute::Moddable)
		    .Field<&StationLayout::displayName>("displayName", kDefined)
		    .Field<&StationLayout::decks>("decks", kDefined)
		    .Field<&StationLayout::piers>("piers", kDefined)
		    .Field<&StationLayout::pads>("pads", kDefined)
		    .Field<&StationLayout::bays>("bays", kDefined)
		    .Field<&StationLayout::reservationSeconds>("reservationSeconds", kDefined);

		RegisterType<Hull>("Dock.Hull").Field<&Hull::definition>("definition", Attribute::Saved);
		RegisterType<Station>("Dock.Station")
		    .Field<&Station::layout>("layout", Attribute::Saved)
		    .Field<&Station::pads>("pads", Attribute::Saved)
		    .Field<&Station::queue>("queue", Attribute::Saved);
		RegisterEnum<PadState>("Dock.PadState")
		    .Value<PadState::Free>("Free")
		    .Value<PadState::Reserved>("Reserved")
		    .Value<PadState::Holding>("Holding");
		RegisterType<Pad>("Dock.Pad")
		    .Field<&Pad::station>("station", Attribute::Saved)
		    .Field<&Pad::number>("number", Attribute::Saved)
		    .Field<&Pad::padClass>("padClass", Attribute::Saved)
		    .Field<&Pad::display>("display", Attribute::Saved)
		    .Field<&Pad::state>("state", Attribute::Saved)
		    .Field<&Pad::ship>("ship", Attribute::Saved)
		    .Field<&Pad::lapses>("lapses", Attribute::Saved);
		RegisterEnum<Stage>("Dock.Stage")
		    .Value<Stage::Queued>("Queued")
		    .Value<Stage::Inbound>("Inbound")
		    .Value<Stage::Descending>("Descending")
		    .Value<Stage::Docked>("Docked")
		    .Value<Stage::Leaving>("Leaving");
		RegisterType<Docking>("Dock.Docking")
		    .Field<&Docking::station>("station", Attribute::Saved)
		    .Field<&Docking::pad>("pad", Attribute::Saved)
		    .Field<&Docking::stage>("stage", Attribute::Saved)
		    .Field<&Docking::fly>("fly", Attribute::Saved);

		RegisterType<RequestPad>("Dock.RequestPad")
		    .Field<&RequestPad::sender>("sender", Attribute::Saved)
		    .Field<&RequestPad::ship>("ship", Attribute::Saved)
		    .Field<&RequestPad::station>("station", Attribute::Saved)
		    .Field<&RequestPad::fly>("fly", Attribute::Saved);
		RegisterType<ReleasePad>("Dock.ReleasePad")
		    .Field<&ReleasePad::sender>("sender", Attribute::Saved)
		    .Field<&ReleasePad::ship>("ship", Attribute::Saved);

		RegisterType<PadGiven>("Dock.PadGiven")
		    .Field<&PadGiven::ship>("ship", Attribute::Saved)
		    .Field<&PadGiven::station>("station", Attribute::Saved)
		    .Field<&PadGiven::pad>("pad", Attribute::Saved)
		    .Field<&PadGiven::number>("number", Attribute::Saved);
		RegisterType<PadQueued>("Dock.PadQueued")
		    .Field<&PadQueued::ship>("ship", Attribute::Saved)
		    .Field<&PadQueued::station>("station", Attribute::Saved)
		    .Field<&PadQueued::place>("place", Attribute::Saved);
		RegisterType<PadRefused>("Dock.PadRefused")
		    .Field<&PadRefused::ship>("ship", Attribute::Saved)
		    .Field<&PadRefused::station>("station", Attribute::Saved)
		    .Field<&PadRefused::reason>("reason", Attribute::Saved);
		RegisterType<PadLapsed>("Dock.PadLapsed")
		    .Field<&PadLapsed::ship>("ship", Attribute::Saved)
		    .Field<&PadLapsed::station>("station", Attribute::Saved)
		    .Field<&PadLapsed::pad>("pad", Attribute::Saved)
		    .Field<&PadLapsed::number>("number", Attribute::Saved);
		RegisterType<PadReleased>("Dock.PadReleased")
		    .Field<&PadReleased::ship>("ship", Attribute::Saved)
		    .Field<&PadReleased::station>("station", Attribute::Saved);
		RegisterType<Landed>("Dock.Landed")
		    .Field<&Landed::ship>("ship", Attribute::Saved)
		    .Field<&Landed::station>("station", Attribute::Saved)
		    .Field<&Landed::pad>("pad", Attribute::Saved)
		    .Field<&Landed::number>("number", Attribute::Saved);
		RegisterType<Left>("Dock.Left")
		    .Field<&Left::ship>("ship", Attribute::Saved)
		    .Field<&Left::station>("station", Attribute::Saved)
		    .Field<&Left::pad>("pad", Attribute::Saved)
		    .Field<&Left::number>("number", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Dock
