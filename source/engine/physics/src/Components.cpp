#include <jettison/physics/Components.h>

#include <jettison/physics/Flight.h>
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
		    .Field<&Body::continuous>("continuous", Attribute::Saved);
		RegisterType<Velocity>("Physics.Velocity")
		    .Field<&Velocity::linear>("linear", Attribute::Saved)
		    .Field<&Velocity::angular>("angular", Attribute::Saved);
		RegisterType<OnRails>("Physics.OnRails").Field<&OnRails::body>("body", Attribute::Saved);
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
