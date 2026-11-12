#include <jettison/render/Atmosphere.h>

#include <jettison/scene/Components.h>

namespace Jettison::Render
{
// The shaders' own source, as C++: atmosphere.glsl and rings.glsl are written in the subset of GLSL
// that GLM accepts.
namespace Glsl
{
using namespace glm;
#include "atmosphere.glsl"
#include "rings.glsl"
} // namespace Glsl

namespace
{
Glsl::Atmosphere ToGlsl(const GpuAtmosphere &atmosphere)
{
	return {atmosphere.centreRadius, atmosphere.rayleigh, atmosphere.mie, atmosphere.shell};
}

Glsl::Rings ToGlsl(const GpuRings &rings)
{
	return {rings.normalInner, rings.colorOuter, rings.shape};
}
} // namespace

GpuAtmosphere PackAtmosphere(
    const Scene::AtmosphereComponent &atmosphere, const glm::vec3 &centre, float radius)
{
	GpuAtmosphere packed;
	packed.centreRadius = glm::vec4{centre, radius};
	packed.rayleigh = glm::vec4{atmosphere.rayleigh, atmosphere.rayleighHeight};
	packed.mie = glm::vec4{
	    atmosphere.mie, atmosphere.mieExtinction, atmosphere.mieHeight, atmosphere.mieAnisotropy};
	packed.shell = glm::vec4{radius + static_cast<float>(atmosphere.height), 1.0f, 0.0f, 0.0f};
	return packed;
}

float AtmosphereColumn(float radius, float mu, float surfaceRadius, float scaleHeight)
{
	return Glsl::AtmosphereColumn(radius, mu, surfaceRadius, scaleHeight);
}

glm::vec3 AtmosphereSunTransmittance(
    const GpuAtmosphere &atmosphere, const glm::vec3 &position, const glm::vec3 &toSun)
{
	return Glsl::AtmosphereSunTransmittance(ToGlsl(atmosphere), position, toSun);
}

AtmosphereSample TraceAtmosphere(
    const GpuAtmosphere &atmosphere, const glm::vec3 &direction, float limit, const glm::vec3 &toSun)
{
	const Glsl::AtmosphereRay ray = Glsl::TraceAtmosphere(ToGlsl(atmosphere), direction, limit, toSun);
	return {ray.inscattered, ray.transmittance};
}

GpuAtmosphere PackAirless(const glm::vec3 &centre, float radius)
{
	GpuAtmosphere packed;
	packed.centreRadius = glm::vec4{centre, radius};
	return packed;
}

GpuRings PackRings(const Scene::RingsComponent &rings, const glm::vec3 &normal)
{
	GpuRings packed;
	packed.normalInner = glm::vec4{glm::normalize(normal), static_cast<float>(rings.innerRadius)};
	packed.colorOuter = glm::vec4{rings.color, static_cast<float>(rings.outerRadius)};
	// The seed is a float in the shader, exact to 2^24.
	packed.shape = glm::vec4{rings.opacity, static_cast<float>(rings.seed % (1u << 24)), 1.0f, 0.0f};
	return packed;
}

float RingCoverage(const GpuRings &rings, float radius, float footprint)
{
	return Glsl::RingCoverage(ToGlsl(rings), radius, footprint);
}

RingCrossing IntersectRings(
    const GpuRings &rings, const glm::vec3 &centre, const glm::vec3 &origin, const glm::vec3 &direction)
{
	const Glsl::RingHit hit = Glsl::RingIntersect(ToGlsl(rings), centre, origin, direction);
	return {hit.distance, hit.radius};
}

float RingSunlight(
    const GpuRings &rings, const glm::vec3 &centre, const glm::vec3 &position, const glm::vec3 &toSun,
    float footprint)
{
	return Glsl::RingSunlight(ToGlsl(rings), centre, position, toSun, footprint);
}

float SphereSunlight(
    const glm::vec4 &centreRadius, const glm::vec3 &position, const glm::vec3 &toSun,
    float sunAngularRadius)
{
	return Glsl::SphereSunlight(centreRadius, position, toSun, sunAngularRadius);
}
} // namespace Jettison::Render
