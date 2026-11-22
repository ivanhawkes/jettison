#include <jettison/dock/Definitions.h>
#include <jettison/dock/Docks.h>

#include <jettison/ai/Types.h>
#include <jettison/physics/Components.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Dock
{
const Serialise::Migrations<Serialise::BinaryRecord> &Berth::Migrations()
{
	static const Serialise::Migrations<Serialise::BinaryRecord> migrations = []
	{
		Serialise::Migrations<Serialise::BinaryRecord> chain{"Dock.Pad", {2}};
		// An enumerator is its name in the archive's table; the class's name is the same text.
		chain.Add(
		    1,
		    [](Serialise::BinaryRecord &record)
		    {
			    const std::string berthClass{record.Open("padClass").ReadName()};
			    record.Set("padClass", berthClass);
		    });
		return chain;
	}();
	return migrations;
}


void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		Physics::RegisterTypes();
		Sim::RegisterTypes();
		constexpr Attribute kDefined = Attribute::Saved | Attribute::Moddable;
		RegisterType<BerthClassDefinition>("Dock.PadClassDefinition", Attribute::Moddable)
		    .Field<&BerthClassDefinition::rank>("rank", kDefined)
		    .Field<&BerthClassDefinition::largestVehicle>("largestHull", kDefined)
		    .Field<&BerthClassDefinition::berthSize>("padSize", kDefined);
		RegisterType<VehicleDefinition>("Dock.HullDefinition", Attribute::Moddable)
		    .Field<&VehicleDefinition::displayName>("displayName", kDefined)
		    .Field<&VehicleDefinition::length>("length", kDefined)
		    .Field<&VehicleDefinition::beam>("beam", kDefined)
		    .Field<&VehicleDefinition::height>("height", kDefined)
		    .Field<&VehicleDefinition::berthClass>("pad", kDefined)
		    .Field<&VehicleDefinition::model>("model", kDefined);
		RegisterType<Box>("Dock.Box")
		    .Field<&Box::centre>("centre", kDefined)
		    .Field<&Box::size>("size", kDefined);
		RegisterType<BerthLayout>("Dock.PadLayout")
		    .Field<&BerthLayout::berthClass>("class", kDefined)
		    .Field<&BerthLayout::position>("position", kDefined)
		    .Field<&BerthLayout::normal>("normal", kDefined)
		    .Field<&BerthLayout::heading>("heading", kDefined)
		    .Field<&BerthLayout::display>("display", kDefined)
		    .Field<&BerthLayout::vehicle>("ship", kDefined)
		    .Field<&BerthLayout::features>("features", kDefined);
		RegisterType<PlatformLayout>("Dock.StationLayout", Attribute::Moddable)
		    .Field<&PlatformLayout::displayName>("displayName", kDefined)
		    .Field<&PlatformLayout::decks>("decks", kDefined)
		    .Field<&PlatformLayout::piers>("piers", kDefined)
		    .Field<&PlatformLayout::berths>("pads", kDefined)
		    .Field<&PlatformLayout::reservationSeconds>("reservationSeconds", kDefined);

		RegisterType<Vehicle>("Dock.Hull").Field<&Vehicle::definition>("definition", Attribute::Saved);
		RegisterType<Platform>("Dock.Station")
		    .Field<&Platform::layout>("layout", Attribute::Saved)
		    .Field<&Platform::berths>("pads", Attribute::Saved)
		    .Field<&Platform::queue>("queue", Attribute::Saved);
		RegisterEnum<BerthState>("Dock.PadState")
		    .Value<BerthState::Free>("Free")
		    .Value<BerthState::Reserved>("Reserved")
		    .Value<BerthState::Holding>("Holding");
		RegisterType<Berth>("Dock.Pad")
		    .Field<&Berth::platform>("station", Attribute::Saved)
		    .Field<&Berth::number>("number", Attribute::Saved)
		    .Field<&Berth::berthClass>("padClass", Attribute::Saved)
		    .Field<&Berth::display>("display", Attribute::Saved)
		    .Field<&Berth::state>("state", Attribute::Saved)
		    .Field<&Berth::vehicle>("ship", Attribute::Saved)
		    .Field<&Berth::lapses>("lapses", Attribute::Saved);
		RegisterEnum<Stage>("Dock.Stage")
		    .Value<Stage::Queued>("Queued")
		    .Value<Stage::Inbound>("Inbound")
		    .Value<Stage::Descending>("Descending")
		    .Value<Stage::Docked>("Docked")
		    .Value<Stage::Leaving>("Leaving");
		RegisterType<Docking>("Dock.Docking")
		    .Field<&Docking::platform>("station", Attribute::Saved)
		    .Field<&Docking::berth>("pad", Attribute::Saved)
		    .Field<&Docking::stage>("stage", Attribute::Saved)
		    .Field<&Docking::fly>("fly", Attribute::Saved)
		    .Field<&Docking::feature>("feature", Attribute::Saved);

		// The retired loading deck's, kept registered for a save's bytes (Docks.h).
		RegisterType<Transfer>("Dock.Transfer")
		    .Field<&Transfer::aboard>("aboard", Attribute::Saved)
		    .Field<&Transfer::ashore>("ashore", Attribute::Saved)
		    .Field<&Transfer::next>("next", Attribute::Saved);
		RegisterType<Buggy>("Dock.Buggy")
		    .Field<&Buggy::platform>("station", Attribute::Saved)
		    .Field<&Buggy::berth>("pad", Attribute::Saved)
		    .Field<&Buggy::vehicle>("ship", Attribute::Saved)
		    .Field<&Buggy::route>("route", Attribute::Saved)
		    .Field<&Buggy::leg>("leg", Attribute::Saved)
		    .Field<&Buggy::along>("along", Attribute::Saved)
		    .Field<&Buggy::hatch>("hatch", Attribute::Saved)
		    .Field<&Buggy::crates>("crates", Attribute::Saved)
		    .Field<&Buggy::outbound>("outbound", Attribute::Saved);

		RegisterType<RequestBerth>("Dock.RequestPad")
		    .Field<&RequestBerth::sender>("sender", Attribute::Saved)
		    .Field<&RequestBerth::vehicle>("ship", Attribute::Saved)
		    .Field<&RequestBerth::platform>("station", Attribute::Saved)
		    .Field<&RequestBerth::fly>("fly", Attribute::Saved)
		    .Field<&RequestBerth::feature>("feature", Attribute::Saved);
		RegisterType<ReleaseBerth>("Dock.ReleasePad")
		    .Field<&ReleaseBerth::sender>("sender", Attribute::Saved)
		    .Field<&ReleaseBerth::vehicle>("ship", Attribute::Saved)
		    .Field<&ReleaseBerth::fly>("fly", Attribute::Saved);

		RegisterEnum<Reason>("Dock.Reason")
		    .Value<Reason::VehicleGone>("ship_gone")
		    .Value<Reason::NotController>("not_controller")
		    .Value<Reason::NotAPlatform>("not_a_station")
		    .Value<Reason::NoVehicle>("no_hull")
		    .Value<Reason::CannotFly>("cannot_fly")
		    .Value<Reason::OtherFrame>("other_system")
		    .Value<Reason::OtherLocalFrame>("other_frame")
		    .Value<Reason::AlreadyQueued>("already_queued")
		    .Value<Reason::AlreadyHoldsBerth>("already_holds_pad")
		    .Value<Reason::NoBerthFits>("no_pad_fits")
		    .Value<Reason::HoldsNothing>("holds_nothing")
		    .Value<Reason::AlreadyLeaving>("already_leaving")
		    .Value<Reason::NoBerthOffers>("no_pad_offers");
		RegisterType<BerthGiven>("Dock.PadGiven", Attribute::Script)
		    .Field<&BerthGiven::vehicle>("ship", Attribute::Saved)
		    .Field<&BerthGiven::platform>("station", Attribute::Saved)
		    .Field<&BerthGiven::berth>("pad", Attribute::Saved)
		    .Field<&BerthGiven::number>("number", Attribute::Saved);
		RegisterType<BerthQueued>("Dock.PadQueued")
		    .Field<&BerthQueued::vehicle>("ship", Attribute::Saved)
		    .Field<&BerthQueued::platform>("station", Attribute::Saved)
		    .Field<&BerthQueued::place>("place", Attribute::Saved);
		RegisterType<BerthRefused>("Dock.PadRefused", Attribute::Script)
		    .Field<&BerthRefused::vehicle>("ship", Attribute::Saved)
		    .Field<&BerthRefused::platform>("station", Attribute::Saved)
		    .Field<&BerthRefused::reason>("reason", Attribute::Saved);
		RegisterType<BerthLapsed>("Dock.PadLapsed")
		    .Field<&BerthLapsed::vehicle>("ship", Attribute::Saved)
		    .Field<&BerthLapsed::platform>("station", Attribute::Saved)
		    .Field<&BerthLapsed::berth>("pad", Attribute::Saved)
		    .Field<&BerthLapsed::number>("number", Attribute::Saved);
		RegisterType<BerthReleased>("Dock.PadReleased")
		    .Field<&BerthReleased::vehicle>("ship", Attribute::Saved)
		    .Field<&BerthReleased::platform>("station", Attribute::Saved);
		RegisterType<Landed>("Dock.Landed", Attribute::Script)
		    .Field<&Landed::vehicle>("ship", Attribute::Saved)
		    .Field<&Landed::platform>("station", Attribute::Saved)
		    .Field<&Landed::berth>("pad", Attribute::Saved)
		    .Field<&Landed::number>("number", Attribute::Saved);
		RegisterType<Left>("Dock.Left", Attribute::Script)
		    .Field<&Left::vehicle>("ship", Attribute::Saved)
		    .Field<&Left::platform>("station", Attribute::Saved)
		    .Field<&Left::berth>("pad", Attribute::Saved)
		    .Field<&Left::number>("number", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Dock
