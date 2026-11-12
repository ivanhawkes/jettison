// The lighting includes the shaders are built from, compiled here as C++: brdf.glsl and
// tonemap.glsl are written in the subset of GLSL that GLM also accepts, so these tests run the
// source every lit program shades with, not a copy of it that could drift.
//
// The reference values are the glTF 2.0 specification's appendix B formulas evaluated
// independently, in double precision, and for the split sum by plain quadrature over the light's
// hemisphere rather than by importance sampling: 800 by 800 midpoints in theta and phi, which is
// converged to better than 1e-4 at every roughness tested. scripts/brdf-reference.py makes them.

#include <doctest/doctest.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace Glsl
{
using namespace glm;
#include "brdf.glsl"
#include "tonemap.glsl"
} // namespace Glsl

using doctest::Approx;

TEST_CASE("BRDF: GGX, Smith and Schlick give the specification's values")
{
	CHECK(Glsl::DistributionGgx(1.0f, 0.25f) == Approx(5.092958).epsilon(1e-5));
	CHECK(Glsl::DistributionGgx(0.9f, 0.5f) == Approx(0.5165482).epsilon(1e-5));
	// Head-on, the visibility term is 1 / 4 at any roughness: G is 1, and the BRDF's denominator 4.
	for (const float alpha : {0.01f, 0.3f, 1.0f})
		CHECK(Glsl::VisibilitySmithGgx(1.0f, 1.0f, alpha) == Approx(0.25).epsilon(1e-6));
	CHECK(Glsl::VisibilitySmithGgx(0.5f, 0.8f, 0.3f) == Approx(0.5808705).epsilon(1e-5));

	const glm::vec3 f0{0.04f, 0.5f, 0.9f};
	CHECK(Glsl::FresnelSchlick(f0, 1.0f) == f0);
	const glm::vec3 grazing = Glsl::FresnelSchlick(f0, 0.0f);
	CHECK(grazing.x == Approx(1.0));
	CHECK(grazing.z == Approx(1.0));
	CHECK(Glsl::FresnelSchlick(glm::vec3{0.04f}, 0.5f).y == Approx(0.07).epsilon(1e-6));
}

TEST_CASE("BRDF: GGX is normalised, its projected area integrating to one")
{
	// D is isotropic, so the integral over half vectors is one-dimensional in theta.
	for (const float alpha : {0.1f, 0.5f, 1.0f})
	{
		CAPTURE(alpha);
		constexpr int kSteps = 20000;
		const double step = glm::half_pi<double>() / kSteps;
		double integral = 0.0;
		for (int index = 0; index < kSteps; ++index)
		{
			const double theta = (index + 0.5) * step;
			integral += Glsl::DistributionGgx(static_cast<float>(std::cos(theta)), alpha) *
			            std::cos(theta) * std::sin(theta) * step * glm::two_pi<double>();
		}
		CHECK(integral == Approx(1.0).epsilon(1e-3));
	}
}

TEST_CASE("BRDF: the metallic-roughness BRDF matches the specification at a reference configuration")
{
	// Base colour (0.8, 0.6, 0.4), metallic 0.25, roughness 0.5; the view and the light on either
	// side of the normal and off its plane.
	Glsl::Surface surface{};
	surface.baseColor = glm::vec3{0.8f, 0.6f, 0.4f};
	surface.metallic = 0.25f;
	surface.roughness = 0.5f;
	const glm::vec3 normal{0.0f, 0.0f, 1.0f};
	const glm::vec3 view = glm::normalize(glm::vec3{0.3f, 0.0f, 1.0f});
	const glm::vec3 light = glm::normalize(glm::vec3{-0.5f, 0.2f, 1.0f});
	const glm::vec3 reflected = Glsl::EvaluateBrdf(surface, normal, view, light);
	CHECK(reflected.x == Approx(0.3279330).epsilon(1e-4));
	CHECK(reflected.y == Approx(0.2587272).epsilon(1e-4));
	CHECK(reflected.z == Approx(0.1853177).epsilon(1e-4));

	// Nothing from behind the surface.
	CHECK(Glsl::EvaluateBrdf(surface, normal, view, -light) == glm::vec3{0.0f});
	// And reciprocal: the BRDF itself, the result over n.l, is the same with view and light swapped.
	const glm::vec3 swapped = Glsl::EvaluateBrdf(surface, normal, light, view);
	const glm::vec3 forward = reflected / glm::dot(normal, light);
	const glm::vec3 backward = swapped / glm::dot(normal, view);
	CHECK(forward.x == Approx(backward.x).epsilon(1e-4));
	CHECK(forward.z == Approx(backward.z).epsilon(1e-4));
}

TEST_CASE("BRDF: the split sum's lookup table integrates the specular lobe")
{
	constexpr unsigned kSamples = 1024;
	// A mirror reflects along one direction, so the scale and bias are Schlick's weights at n.v.
	const glm::vec2 mirror = Glsl::IntegrateSpecularBrdf(0.5f, 0.0f, kSamples);
	CHECK(mirror.x == Approx(0.96875).epsilon(1e-4));
	CHECK(mirror.y == Approx(0.03125).epsilon(1e-4));

	struct Reference
	{
		float nDotV;
		float roughness;
		double scale;
		double bias;
	};
	for (const Reference reference : {
	         Reference{0.5f, 0.5f, 0.8349166, 0.0223474},
	         Reference{0.25f, 0.75f, 0.7165086, 0.0279484},
	         Reference{0.9f, 0.3f, 0.9893175, 0.0001064},
	         Reference{0.7f, 1.0f, 0.3779877, 0.0009004},
	     })
	{
		CAPTURE(reference.nDotV);
		CAPTURE(reference.roughness);
		const glm::vec2 integrated =
		    Glsl::IntegrateSpecularBrdf(reference.nDotV, reference.roughness, kSamples);
		CHECK(integrated.x == Approx(reference.scale).epsilon(0.005));
		CHECK(integrated.y == Approx(reference.bias).epsilon(0.002).scale(1.0));
	}
}

TEST_CASE("BRDF: the samplers draw what the bake passes take them to")
{
	// The Hammersley set's second coordinate is the index's bits reversed.
	CHECK(Glsl::Hammersley(1u, 4u) == glm::vec2{0.25f, 0.5f});
	CHECK(Glsl::Hammersley(3u, 4u) == glm::vec2{0.75f, 0.75f});
	CHECK(Glsl::Hammersley(2u, 8u).y == 0.25f);
	for (unsigned index = 0; index < 64; ++index)
	{
		const glm::vec2 xi = Glsl::Hammersley(index, 64u);
		CHECK(glm::length(Glsl::ImportanceSampleGgx(xi, 0.4f)) == Approx(1.0).epsilon(1e-5));
		const glm::vec3 cosine = Glsl::ImportanceSampleCosine(xi);
		CHECK(glm::length(cosine) == Approx(1.0).epsilon(1e-5));
		CHECK(cosine.z >= 0.0f);
	}
	// A mirror's half vectors are the normal.
	CHECK(Glsl::ImportanceSampleGgx(glm::vec2{0.3f, 0.7f}, 0.0f).z == Approx(1.0));
}

TEST_CASE("Tonemap: PBR Neutral keeps colours below its knee and compresses what is above it")
{
	// Black stays black, and below the knee a colour is itself less the 0.04 offset.
	CHECK(Glsl::TonemapPbrNeutral(glm::vec3{0.0f}) == glm::vec3{0.0f});
	const glm::vec3 grey = Glsl::TonemapPbrNeutral(glm::vec3{0.5f});
	CHECK(grey.x == Approx(0.46));
	const glm::vec3 red = Glsl::TonemapPbrNeutral(glm::vec3{0.6f, 0.2f, 0.1f});
	CHECK(red.x == Approx(0.56));
	CHECK(red.y == Approx(0.16));
	CHECK(red.z == Approx(0.06));

	// Brighter is brighter, all the way up, and never past white.
	float last = 0.0f;
	for (float value = 0.01f; value < 10000.0f; value *= 1.5f)
	{
		CAPTURE(value);
		const glm::vec3 mapped = Glsl::TonemapPbrNeutral(glm::vec3{value});
		CHECK(mapped.x > last);
		CHECK(mapped.x < 1.0f);
		last = mapped.x;
	}
	CHECK(last > 0.999f);
	// A saturated highlight is pulled towards white rather than clipped to its hue.
	const glm::vec3 hot = Glsl::TonemapPbrNeutral(glm::vec3{40.0f, 4.0f, 0.4f});
	CHECK(hot.x < 1.0f);
	CHECK(hot.z > 0.1f);
}

TEST_CASE("BRDF: a lit particle's phase law is a Lambertian sphere's disc, averaged")
{
	// The sphere's visible disc, by brute force: each point of its viewer-facing hemisphere shows
	// albedo / pi times the irradiance it gets, max(n.l, 0) under light of irradiance one, weighted
	// by its projected area n.v, over the disc's area pi. In units of 1 / pi, the mean is the
	// integral of max(n.l, 0) n.v over the hemisphere, over pi.
	const auto bruteForce = [](double phase)
	{
		const glm::dvec3 toViewer{0.0, 0.0, 1.0};
		const glm::dvec3 toLight{std::sin(phase), 0.0, std::cos(phase)};
		constexpr int kSteps = 400;
		double sum = 0.0;
		for (int i = 0; i < kSteps; ++i)
			for (int j = 0; j < 2 * kSteps; ++j)
			{
				const double theta = (i + 0.5) / kSteps * glm::half_pi<double>();
				const double phi = (j + 0.5) / (2 * kSteps) * glm::two_pi<double>();
				const glm::dvec3 normal{
				    std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
				const double area = std::sin(theta) * (glm::half_pi<double>() / kSteps) *
				                    (glm::two_pi<double>() / (2 * kSteps));
				sum += std::max(glm::dot(normal, toLight), 0.0) * glm::dot(normal, toViewer) * area;
			}
		return sum / glm::pi<double>();
	};
	CHECK(Glsl::LambertSpherePhase(1.0f) == Approx(2.0 / 3.0).epsilon(1e-5));
	CHECK(Glsl::LambertSpherePhase(-1.0f) == Approx(0.0).epsilon(1e-6));
	for (const double phase : {0.3, 1.0, 1.5707963, 2.4})
		CHECK(
		    Glsl::LambertSpherePhase(static_cast<float>(std::cos(phase))) ==
		    Approx(bruteForce(phase)).epsilon(1e-3));
}
