#include <jettison/space/StarSystemFrame.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Space
{
namespace
{
// A body that pulls, where it is at a time.
struct Well
{
	glm::dvec3 position;
	double gravitationalParameter;
	double radius;
};


class StarSystemFrame final : public Physics::FrameModel
{
  public:
	explicit StarSystemFrame(Celestial::StarSystem system) : m_system{std::move(system)}
	{
	}


	bool HasBody(std::string_view body) const override
	{
		return m_system.Find(body) >= 0;
	}


	Sim::Transform BodyAt(std::string_view body, double simSeconds) const override
	{
		const auto index = static_cast<std::size_t>(m_system.Find(body));
		return {
		    Celestial::EvaluateBody(m_system, index, simSeconds).metres,
		    Celestial::BodyOrientation(m_system, index, simSeconds)};
	}


	std::function<glm::dvec3(const glm::dvec3 &)> GravityAt(double simSeconds) const override
	{
		std::vector<Well> wells;
		const std::vector<Celestial::SystemPosition> positions =
		    Celestial::EvaluateSystem(m_system, simSeconds);
		for (std::size_t body = 0; body < m_system.bodies.size(); ++body)
			if (m_system.bodies[body].gravitationalParameter > 0.0)
				wells.push_back(
				    {positions[body].metres, m_system.bodies[body].gravitationalParameter,
				     m_system.bodies[body].radius});
		return [wells = std::move(wells)](const glm::dvec3 &position)
		{
			glm::dvec3 acceleration{0.0};
			for (const Well &well : wells)
			{
				const glm::dvec3 towards = well.position - position;
				const double distance = glm::length(towards);
				if (distance <= 0.0)
					continue;
				// Inside the body, the mass within the distance pulls, as a uniform sphere's does, so
				// the pull falls to nothing at the centre rather than growing without bound.
				const double reach = std::max(distance, well.radius);
				acceleration += towards * (well.gravitationalParameter / (reach * reach * reach));
			}
			return acceleration;
		};
	}

  private:
	Celestial::StarSystem m_system;
};
} // namespace


void DefineStarSystem(Physics::PhysicsWorld &physics, Celestial::StarSystem system)
{
	if (system.name.empty() || system.bodies.empty())
		throw std::invalid_argument{"Space: a star system needs a name and bodies"};
	for (std::size_t body = 1; body < system.bodies.size(); ++body)
		if (system.bodies[body].parentIndex < 0)
			throw std::invalid_argument{std::format(
			    "Space: star system '{}' is not resolved: '{}' has no parent", system.name,
			    system.bodies[body].name)};
	const std::uint64_t frame = Sim::FrameId(system.name);
	if (physics.HasFrame(frame))
		throw std::invalid_argument{
		    std::format("Space: star system '{}' is already defined", system.name)};
	physics.DefineFrame(frame, std::make_unique<StarSystemFrame>(std::move(system)));
}
} // namespace Jettison::Space
