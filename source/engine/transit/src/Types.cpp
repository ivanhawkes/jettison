#include <jettison/transit/Types.h>

#include <jettison/physics/Components.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/transit/Passage.h>
#include <jettison/transit/Profile.h>

namespace Jettison::Transit
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		Physics::RegisterTypes();
		constexpr Attribute kDefined = Attribute::Saved | Attribute::Moddable;
		RegisterType<MotionProfile>("Ai.CruiseProfile")
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
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Transit
