#pragma once

#include <glm/glm.hpp>

#include <jettison/render/FrameData.h>

namespace Jettison::Scene
{
struct AtmosphereComponent;
struct RingsComponent;
} // namespace Jettison::Scene

namespace Jettison::Render
{
// A body's atmosphere and rings on the CPU (docs/architecture.md, "Atmospheres"). The functions are
// assets/shaders/include/atmosphere.glsl and rings.glsl compiled as C++, the source the planet and
// atmosphere programs run, so the render suite checks that source rather than a copy of it. Lengths
// are metres in render space, the camera at the origin.

// The atmosphere of a body of the radius given, whose centre is where it is in render space, as the
// programs read it.
GpuAtmosphere PackAtmosphere(
    const Scene::AtmosphereComponent &atmosphere, const glm::vec3 &centre, float radius);

// The column of air of one kind along a ray to space from a point at a radius from the body's centre,
// at the cosine of its angle from the zenith, in metres of air at the surface's density.
float AtmosphereColumn(float radius, float mu, float surfaceRadius, float scaleHeight);

// The share of the sun's light, by colour, that reaches a point in render space.
glm::vec3 AtmosphereSunTransmittance(
    const GpuAtmosphere &atmosphere, const glm::vec3 &position, const glm::vec3 &toSun);

// What a ray from the camera picks up on its way to what it meets at the distance given: the light
// scattered towards the camera per unit of the sun's irradiance, and the share of what is behind
// that gets through.
struct AtmosphereSample
{
	glm::vec3 inscattered{0.0f};
	glm::vec3 transmittance{1.0f};
};
AtmosphereSample TraceAtmosphere(
    const GpuAtmosphere &atmosphere, const glm::vec3 &direction, float limit, const glm::vec3 &toSun);

// A body with no air, at its centre and radius: the entry of the frame's list a body with rings
// alone has.
GpuAtmosphere PackAirless(const glm::vec3 &centre, float radius);

// The rings of a body whose spin axis, the normal of their plane, is the one given in render space.
GpuRings PackRings(const Scene::RingsComponent &rings, const glm::vec3 &normal);

// The share of the light the rings stop face on at a radius from the body's centre, where a pixel
// covers the footprint given of their width.
float RingCoverage(const GpuRings &rings, float radius, float footprint);

// Where a ray meets the rings' plane: the distance along it, negative for none ahead, and the
// radius from the centre there.
struct RingCrossing
{
	float distance{-1.0f};
	float radius{0.0f};
};
RingCrossing IntersectRings(
    const GpuRings &rings, const glm::vec3 &centre, const glm::vec3 &origin,
    const glm::vec3 &direction);

// The share of the sun's light the rings let through to a point spanning the footprint given in
// metres, and the share a sphere leaves of the sun's disc, of the angular radius given.
float RingSunlight(
    const GpuRings &rings, const glm::vec3 &centre, const glm::vec3 &position, const glm::vec3 &toSun,
    float footprint = 0.0f);
float SphereSunlight(
    const glm::vec4 &centreRadius, const glm::vec3 &position, const glm::vec3 &toSun,
    float sunAngularRadius);
} // namespace Jettison::Render
