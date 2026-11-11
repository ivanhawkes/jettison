#include <jettison/physics/Components.h>

#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/physics/Shape.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Physics
{
void RegisterTypes()
{
	// The tables are global, and a process may hold more than one world.
	static const bool registered = []
	{
		using namespace Reflect;
		Sim::RegisterTypes();
		RegisterEnum<BodyKind>("Physics.BodyKind")
		    .Value<BodyKind::Static>("Static")
		    .Value<BodyKind::Kinematic>("Kinematic")
		    .Value<BodyKind::Dynamic>("Dynamic");
		RegisterType<Body>("Physics.Body")
		    .Field<&Body::kind>("kind", Attribute::Saved)
		    .Field<&Body::shape>("shape", Attribute::Saved)
		    .Field<&Body::mass>("mass", Attribute::Saved)
		    .Field<&Body::friction>("friction", Attribute::Saved)
		    .Field<&Body::restitution>("restitution", Attribute::Saved)
		    .Field<&Body::linearDamping>("linearDamping", Attribute::Saved)
		    .Field<&Body::angularDamping>("angularDamping", Attribute::Saved)
		    .Field<&Body::continuous>("continuous", Attribute::Saved)
		    .Field<&Body::sensor>("sensor", Attribute::Saved);
		RegisterType<Velocity>("Physics.Velocity")
		    .Field<&Velocity::linear>("linear", Attribute::Saved)
		    .Field<&Velocity::angular>("angular", Attribute::Saved);
		RegisterType<OnRails>("Physics.OnRails")
		    .Field<&OnRails::body>("body", Attribute::Saved)
		    .Field<&OnRails::offset>("offset", Attribute::Saved)
		    .Field<&OnRails::turn>("turn", Attribute::Saved);
		RegisterType<LocalFrame>("Physics.LocalFrame")
		    .Field<&LocalFrame::body>("body", Attribute::Saved);

		RegisterType<Thrusters>("Physics.Thrusters")
		    .Field<&Thrusters::forward>("forward", Attribute::Saved)
		    .Field<&Thrusters::reverse>("reverse", Attribute::Saved)
		    .Field<&Thrusters::lateral>("lateral", Attribute::Saved)
		    .Field<&Thrusters::vertical>("vertical", Attribute::Saved)
		    .Field<&Thrusters::angularAcceleration>("angularAcceleration", Attribute::Saved)
		    .Field<&Thrusters::maxSpeed>("maxSpeed", Attribute::Saved)
		    .Field<&Thrusters::maxAngularRate>("maxAngularRate", Attribute::Saved);
		RegisterType<FlightControls>("Physics.FlightControls")
		    .Field<&FlightControls::linear>("linear", Attribute::Saved)
		    .Field<&FlightControls::angular>("angular", Attribute::Saved)
		    .Field<&FlightControls::assist>("assist", Attribute::Saved)
		    .Field<&FlightControls::reference>("reference", Attribute::Saved);
		RegisterType<SetFlightControls>("Physics.SetFlightControls")
		    .Field<&SetFlightControls::ship>("ship", Attribute::Saved)
		    .Field<&SetFlightControls::controls>("controls", Attribute::Saved);

		RegisterEnum<ContactPhase>("Physics.ContactPhase")
		    .Value<ContactPhase::Began>("Began")
		    .Value<ContactPhase::Ended>("Ended");
		RegisterType<ContactEvent>("Physics.ContactEvent")
		    .Field<&ContactEvent::phase>("phase", Attribute::Saved)
		    .Field<&ContactEvent::first>("first", Attribute::Saved)
		    .Field<&ContactEvent::second>("second", Attribute::Saved)
		    .Field<&ContactEvent::sensor>("sensor", Attribute::Saved)
		    .Field<&ContactEvent::point>("point", Attribute::Saved)
		    .Field<&ContactEvent::normal>("normal", Attribute::Saved)
		    .Field<&ContactEvent::speed>("speed", Attribute::Saved);
		RegisterType<Pick>("Physics.Pick")
		    .Field<&Pick::request>("request", Attribute::Saved)
		    .Field<&Pick::system>("system", Attribute::Saved)
		    .Field<&Pick::origin>("origin", Attribute::Saved)
		    .Field<&Pick::direction>("direction", Attribute::Saved)
		    .Field<&Pick::maxDistance>("maxDistance", Attribute::Saved)
		    .Field<&Pick::time>("time", Attribute::Saved)
		    .Field<&Pick::ignore>("ignore", Attribute::Saved);
		RegisterType<Picked>("Physics.Picked")
		    .Field<&Picked::request>("request", Attribute::Saved)
		    .Field<&Picked::entity>("entity", Attribute::Saved)
		    .Field<&Picked::distance>("distance", Attribute::Saved)
		    .Field<&Picked::point>("point", Attribute::Saved)
		    .Field<&Picked::normal>("normal", Attribute::Saved);

		RegisterEnum<ShapeKind>("Physics.ShapeKind")
		    .Value<ShapeKind::Box>("Box")
		    .Value<ShapeKind::Sphere>("Sphere")
		    .Value<ShapeKind::Capsule>("Capsule")
		    .Value<ShapeKind::Cylinder>("Cylinder")
		    .Value<ShapeKind::ConvexHull>("ConvexHull")
		    .Value<ShapeKind::Mesh>("Mesh");
		constexpr Attribute kDefined = Attribute::Saved | Attribute::Moddable;
		RegisterType<ShapeDefinition>("Physics.Shape", Attribute::Moddable)
		    .Field<&ShapeDefinition::kind>("kind", kDefined)
		    .Field<&ShapeDefinition::halfExtents>("halfExtents", kDefined)
		    .Field<&ShapeDefinition::radius>("radius", kDefined)
		    .Field<&ShapeDefinition::halfHeight>("halfHeight", kDefined)
		    .Field<&ShapeDefinition::points>("points", kDefined)
		    .Field<&ShapeDefinition::vertices>("vertices", kDefined)
		    .Field<&ShapeDefinition::indices>("indices", kDefined);
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Physics
