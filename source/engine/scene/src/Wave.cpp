#include <jettison/scene/Wave.h>

#include <cmath>
#include <numbers>

#include <jettison/reflect/Reflect.h>
#include <jettison/scene/Components.h>

namespace Jettison::Scene
{
float EvaluateWave(Wave wave, double cycles)
{
	const double at = cycles - std::floor(cycles);
	switch (wave)
	{
		case Wave::Sine: return static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * at));
		case Wave::Triangle: return static_cast<float>(1.0 - std::abs(1.0 - 2.0 * at));
		case Wave::Saw: return static_cast<float>(at);
		case Wave::Square: return at < 0.5 ? 0.0f : 1.0f;
	}
	return 0.0f;
}


float LightIntensityAt(const LightComponent &light, double time)
{
	if (!light.animation)
		return light.intensity;
	const LightAnimation &animation = *light.animation;
	const float at = EvaluateWave(animation.wave, time / animation.period + animation.phase);
	return light.intensity * (animation.from + (animation.to - animation.from) * at);
}


void RegisterWave()
{
	static const bool registered = []
	{
		Reflect::RegisterEnum<Wave>("Scene.Wave")
		    .Value<Wave::Sine>("Sine")
		    .Value<Wave::Triangle>("Triangle")
		    .Value<Wave::Saw>("Saw")
		    .Value<Wave::Square>("Square");
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Scene
