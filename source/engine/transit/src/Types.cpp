#include <jettison/transit/Types.h>

#include <jettison/physics/Components.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/transit/Passage.h>
#include <jettison/transit/Portal.h>
#include <jettison/transit/Profile.h>
#include <jettison/transit/Slot.h>

namespace Jettison::Transit
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		Physics::RegisterTypes();
		constexpr Attribute kDefined = Attribute::Saved | Attribute::Moddable;
		RegisterType<MotionProfile>("Transit.MotionProfile")
		    .Field<&MotionProfile::boostAcceleration>("boostAcceleration", kDefined)
		    .Field<&MotionProfile::boostSeconds>("boostSeconds", kDefined)
		    .Field<&MotionProfile::sustainedAcceleration>("sustainedAcceleration", kDefined)
		    .Field<&MotionProfile::thrustShare>("thrustShare", kDefined);
		RegisterType<Passage>("Transit.Passage")
		    .Field<&Passage::frame>("frame", Attribute::Saved)
		    .Field<&Passage::carrier>("carrier", Attribute::Saved)
		    .Field<&Passage::path>("path", Attribute::Saved)
		    .Field<&Passage::departureStep>("departureStep", Attribute::Saved)
		    .Field<&Passage::arrivalStep>("arrivalStep", Attribute::Saved)
		    .Field<&Passage::body>("body", Attribute::Saved)
		    .Field<&Passage::profile>("profile", Attribute::Saved);
		RegisterType<PassageEnded>("Transit.PassageEnded", Attribute::Script)
		    .Field<&PassageEnded::entity>("entity", Attribute::Saved);
		RegisterType<InPortal>("Transit.InPortal")
		    .Field<&InPortal::from>("from", Attribute::Saved)
		    .Field<&InPortal::entrance>("entrance", Attribute::Saved)
		    .Field<&InPortal::to>("to", Attribute::Saved)
		    .Field<&InPortal::exit>("exit", Attribute::Saved)
		    .Field<&InPortal::arrivalStep>("arrivalStep", Attribute::Saved);
		RegisterType<PortalLeft>("Transit.PortalLeft", Attribute::Script)
		    .Field<&PortalLeft::entity>("entity", Attribute::Saved)
		    .Field<&PortalLeft::frame>("frame", Attribute::Saved)
		    .Field<&PortalLeft::exit>("exit", Attribute::Saved);
		RegisterType<Slot>("Transit.Slot")
		    .Field<&Slot::holder>("holder", Attribute::Saved)
		    .Field<&Slot::lapses>("lapses", Attribute::Saved)
		    .Field<&Slot::queue>("queue", Attribute::Saved);
		RegisterType<Claim>("Transit.Claim")
		    .Field<&Claim::slot>("slot", Attribute::Saved)
		    .Field<&Claim::held>("held", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Transit
