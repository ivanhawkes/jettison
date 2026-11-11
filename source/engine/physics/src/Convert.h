#pragma once

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Real.h>
#include <Jolt/Math/Vec3.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <jettison/sim/Snapshot.h>

namespace Jettison::Physics::Detail
{
// Between GLM and Jolt. Positions are doubles both sides (JPH_DOUBLE_PRECISION); a rotation is a
// float quaternion in Jolt, so a Transform's orientation loses its double precision on the way in
// and is a float's value exactly on the way out.
inline JPH::RVec3 ToJolt(const glm::dvec3 &value)
{
	return {value.x, value.y, value.z};
}
inline JPH::Vec3 ToJolt(const glm::vec3 &value)
{
	return {value.x, value.y, value.z};
}
inline JPH::Quat ToJolt(const glm::dquat &value)
{
	return {
	    static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z),
	    static_cast<float>(value.w)};
}

inline glm::dvec3 ToGlm(JPH::RVec3Arg value)
{
	return {value.GetX(), value.GetY(), value.GetZ()};
}
inline glm::vec3 ToGlmFloat(JPH::Vec3Arg value)
{
	return {value.GetX(), value.GetY(), value.GetZ()};
}
inline glm::dquat ToGlm(JPH::QuatArg value)
{
	return {value.GetW(), value.GetX(), value.GetY(), value.GetZ()};
}

// Exact: what physics placed a body at is compared with what is there now, to see whether the
// game wrote it.
inline bool SameTransform(const Sim::Transform &left, const Sim::Transform &right)
{
	return left.position == right.position && left.orientation == right.orientation;
}
} // namespace Jettison::Physics::Detail
