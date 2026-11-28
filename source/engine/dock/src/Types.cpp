#include <jettison/dock/Definitions.h>
#include <jettison/dock/Docks.h>

#include <jettison/ai/Types.h>
#include <jettison/physics/Components.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>
#include <jettison/sim/Snapshot.h>

#include <initializer_list>
#include <string_view>
#include <utility>

namespace Jettison::Dock
{
const Serialise::Migrations<Serialise::BinaryRecord> &Berth::Migrations()
{
	static const Serialise::Migrations<Serialise::BinaryRecord> migrations{"Dock.Berth", {3}};
	return migrations;
}


const Serialise::Migrations<Serialise::BinaryRecord> &Platform::Migrations()
{
	static const Serialise::Migrations<Serialise::BinaryRecord> migrations{"Dock.Platform", {2}};
	return migrations;
}


const Serialise::Migrations<Serialise::BinaryRecord> &Docking::Migrations()
{
	static const Serialise::Migrations<Serialise::BinaryRecord> migrations{"Dock.Docking", {2}};
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
		RegisterType<BerthClassDefinition>("Dock.BerthClassDefinition", Attribute::Moddable)
		    .Field<&BerthClassDefinition::rank>("rank", kDefined)
		    .Field<&BerthClassDefinition::largestVehicle>("largestVehicle", kDefined)
		    .Field<&BerthClassDefinition::berthSize>("berthSize", kDefined);
		RegisterType<VehicleDefinition>("Dock.VehicleDefinition", Attribute::Moddable)
		    .Field<&VehicleDefinition::displayName>("displayName", kDefined)
		    .Field<&VehicleDefinition::length>("length", kDefined)
		    .Field<&VehicleDefinition::beam>("beam", kDefined)
		    .Field<&VehicleDefinition::height>("height", kDefined)
		    .Field<&VehicleDefinition::berthClass>("berth", kDefined)
		    .Field<&VehicleDefinition::model>("model", kDefined)
		    .Field<&VehicleDefinition::collision>("collision", kDefined);
		RegisterType<Box>("Dock.Box")
		    .Field<&Box::centre>("centre", kDefined)
		    .Field<&Box::size>("size", kDefined);
		RegisterType<BerthLayout>("Dock.BerthLayout")
		    .Field<&BerthLayout::berthClass>("class", kDefined)
		    .Field<&BerthLayout::position>("position", kDefined)
		    .Field<&BerthLayout::normal>("normal", kDefined)
		    .Field<&BerthLayout::heading>("heading", kDefined)
		    .Field<&BerthLayout::display>("display", kDefined)
		    .Field<&BerthLayout::vehicle>("vehicle", kDefined)
		    .Field<&BerthLayout::features>("features", kDefined);
		RegisterType<PlatformLayout>("Dock.PlatformLayout", Attribute::Moddable)
		    .Field<&PlatformLayout::displayName>("displayName", kDefined)
		    .Field<&PlatformLayout::decks>("decks", kDefined)
		    .Field<&PlatformLayout::piers>("piers", kDefined)
		    .Field<&PlatformLayout::berths>("berths", kDefined)
		    .Field<&PlatformLayout::reservationSeconds>("reservationSeconds", kDefined);

		RegisterType<Vehicle>("Dock.Vehicle")
		    .Field<&Vehicle::definition>("definition", Attribute::Saved);
		RegisterType<Platform>("Dock.Platform")
		    .Field<&Platform::layout>("layout", Attribute::Saved)
		    .Field<&Platform::berths>("berths", Attribute::Saved)
		    .Field<&Platform::queue>("queue", Attribute::Saved);
		RegisterEnum<BerthState>("Dock.BerthState")
		    .Value<BerthState::Free>("Free")
		    .Value<BerthState::Reserved>("Reserved")
		    .Value<BerthState::Holding>("Holding");
		RegisterType<Berth>("Dock.Berth")
		    .Field<&Berth::platform>("platform", Attribute::Saved)
		    .Field<&Berth::number>("number", Attribute::Saved)
		    .Field<&Berth::berthClass>("berthClass", Attribute::Saved)
		    .Field<&Berth::display>("display", Attribute::Saved)
		    .Field<&Berth::state>("state", Attribute::Saved)
		    .Field<&Berth::vehicle>("vehicle", Attribute::Saved)
		    .Field<&Berth::lapses>("lapses", Attribute::Saved)
		    .Field<&Berth::nearest>("nearest", Attribute::Saved);
		RegisterEnum<Stage>("Dock.Stage")
		    .Value<Stage::Queued>("Queued")
		    .Value<Stage::Inbound>("Inbound")
		    .Value<Stage::Descending>("Descending")
		    .Value<Stage::Docked>("Docked")
		    .Value<Stage::Leaving>("Leaving");
		RegisterType<Docking>("Dock.Docking")
		    .Field<&Docking::platform>("platform", Attribute::Saved)
		    .Field<&Docking::berth>("berth", Attribute::Saved)
		    .Field<&Docking::stage>("stage", Attribute::Saved)
		    .Field<&Docking::fly>("fly", Attribute::Saved)
		    .Field<&Docking::feature>("feature", Attribute::Saved);

		RegisterType<RequestBerth>("Dock.RequestBerth")
		    .Field<&RequestBerth::sender>("sender", Attribute::Saved)
		    .Field<&RequestBerth::vehicle>("vehicle", Attribute::Saved)
		    .Field<&RequestBerth::platform>("platform", Attribute::Saved)
		    .Field<&RequestBerth::fly>("fly", Attribute::Saved)
		    .Field<&RequestBerth::feature>("feature", Attribute::Saved);
		RegisterType<ReleaseBerth>("Dock.ReleaseBerth")
		    .Field<&ReleaseBerth::sender>("sender", Attribute::Saved)
		    .Field<&ReleaseBerth::vehicle>("vehicle", Attribute::Saved)
		    .Field<&ReleaseBerth::fly>("fly", Attribute::Saved);

		RegisterEnum<Reason>("Dock.Reason")
		    .Value<Reason::VehicleGone>("vehicle_gone")
		    .Value<Reason::NotController>("not_controller")
		    .Value<Reason::NotAPlatform>("not_a_platform")
		    .Value<Reason::NoVehicle>("no_vehicle")
		    .Value<Reason::CannotFly>("cannot_fly")
		    .Value<Reason::OtherFrame>("other_frame")
		    .Value<Reason::OtherLocalFrame>("other_local_frame")
		    .Value<Reason::AlreadyQueued>("already_queued")
		    .Value<Reason::AlreadyHoldsBerth>("already_holds_berth")
		    .Value<Reason::NoBerthFits>("no_berth_fits")
		    .Value<Reason::HoldsNothing>("holds_nothing")
		    .Value<Reason::AlreadyLeaving>("already_leaving")
		    .Value<Reason::NoBerthOffers>("no_berth_offers");
		RegisterType<BerthGiven>("Dock.BerthGiven", Attribute::Script)
		    .Field<&BerthGiven::vehicle>("vehicle", Attribute::Saved)
		    .Field<&BerthGiven::platform>("platform", Attribute::Saved)
		    .Field<&BerthGiven::berth>("berth", Attribute::Saved)
		    .Field<&BerthGiven::number>("number", Attribute::Saved);
		RegisterType<BerthQueued>("Dock.BerthQueued")
		    .Field<&BerthQueued::vehicle>("vehicle", Attribute::Saved)
		    .Field<&BerthQueued::platform>("platform", Attribute::Saved)
		    .Field<&BerthQueued::place>("place", Attribute::Saved);
		RegisterType<BerthRefused>("Dock.BerthRefused", Attribute::Script)
		    .Field<&BerthRefused::vehicle>("vehicle", Attribute::Saved)
		    .Field<&BerthRefused::platform>("platform", Attribute::Saved)
		    .Field<&BerthRefused::reason>("reason", Attribute::Saved);
		RegisterType<BerthLapsed>("Dock.BerthLapsed")
		    .Field<&BerthLapsed::vehicle>("vehicle", Attribute::Saved)
		    .Field<&BerthLapsed::platform>("platform", Attribute::Saved)
		    .Field<&BerthLapsed::berth>("berth", Attribute::Saved)
		    .Field<&BerthLapsed::number>("number", Attribute::Saved);
		RegisterType<BerthReleased>("Dock.BerthReleased")
		    .Field<&BerthReleased::vehicle>("vehicle", Attribute::Saved)
		    .Field<&BerthReleased::platform>("platform", Attribute::Saved);
		RegisterType<Landed>("Dock.Landed", Attribute::Script)
		    .Field<&Landed::vehicle>("vehicle", Attribute::Saved)
		    .Field<&Landed::platform>("platform", Attribute::Saved)
		    .Field<&Landed::berth>("berth", Attribute::Saved)
		    .Field<&Landed::number>("number", Attribute::Saved);
		RegisterType<Left>("Dock.Left", Attribute::Script)
		    .Field<&Left::vehicle>("vehicle", Attribute::Saved)
		    .Field<&Left::platform>("platform", Attribute::Saved)
		    .Field<&Left::berth>("berth", Attribute::Saved)
		    .Field<&Left::number>("number", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Dock
