#include <jettison/app/CelestialPresentation.h>

#include <stdexcept>
#include <string>

#include <jettison/render/Scene.h>

namespace Jettison::App
{
void ResolveCelestialBodies(const entt::registry &presentation, const Celestial::StarSystem &system)
{
	for (const auto [entity, celestial] :
	     presentation.view<const Render::CelestialBodyComponent>().each())
		if (system.Find(celestial.body) < 0)
			throw std::runtime_error(
			    "The scene names celestial body '" + celestial.body + "', which star system '" +
			    system.name + "' does not have");
}

void SyncCelestialBodies(
    entt::registry &presentation, const Celestial::StarSystem &system, double simSeconds)
{
	for (const auto [entity, celestial] :
	     presentation.view<const Render::CelestialBodyComponent>().each())
	{
		const int index = system.Find(celestial.body);
		if (index < 0)
			continue;
		const auto body = static_cast<std::size_t>(index);
		presentation.emplace_or_replace<Render::PlacementComponent>(
		    entity, Celestial::EvaluateBody(system, body, simSeconds).metres,
		    Celestial::Orientation(system.bodies[body].rotation, simSeconds),
		    system.bodies[body].radius);
	}
}
} // namespace Jettison::App
