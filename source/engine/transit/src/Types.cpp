#include <jettison/transit/Types.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/transit/Profile.h>

namespace Jettison::Transit
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		constexpr Attribute kDefined = Attribute::Saved | Attribute::Moddable;
		RegisterType<MotionProfile>("Ai.CruiseProfile")
		    .Field<&MotionProfile::boostAcceleration>("boostAcceleration", kDefined)
		    .Field<&MotionProfile::boostSeconds>("boostSeconds", kDefined)
		    .Field<&MotionProfile::sustainedAcceleration>("sustainedAcceleration", kDefined)
		    .Field<&MotionProfile::thrustShare>("thrustShare", kDefined);
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Transit
