#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace Jettison::Physics
{
// Jolt in double precision, stepped inside the simulation's fixed step, with contact and sensor
// events delivered to the simulation. Jolt's types stay inside this module; the engine sees
// bodies by entity. Phase 7 fills this in, with the ship-hits-station test as its exit.
enum class BodyKind : std::uint8_t
{
	Static,
	Dynamic,
	Sensor
};

struct ShapeDescription
{
	enum class Kind : std::uint8_t
	{
		Box,
		Sphere,
		Capsule,
		Cylinder,
		ConvexHull,
		Mesh
	} kind{Kind::Box};
	glm::dvec3 halfExtents{1.0};
};

struct RayHit
{
	entt::entity entity{entt::null};
	double distance{0.0};
	glm::dvec3 normal{0.0};
};

class PhysicsWorld
{
  public:
	void AddBody(
	    entt::entity entity, BodyKind kind, const ShapeDescription &shape, const glm::dvec3 &position);
	void RemoveBody(entt::entity entity);
	void Step(double fixedStep);
	std::optional<RayHit> CastRay(
	    const glm::dvec3 &origin, const glm::dvec3 &direction, double maxDistance) const;
	std::size_t BodyCount() const
	{
		return m_bodyCount;
	}

  private:
	std::size_t m_bodyCount{0};
};
} // namespace Jettison::Physics
