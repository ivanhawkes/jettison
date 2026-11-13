// The post passes' arithmetic (docs/architecture.md, "Post-processing" and "Outlines"): a pixel's
// view-space position from the reversed, infinite depth; the ambient obscurance one sample of the
// neighbourhood adds; the bloom chain's filters; the jump flood's choice of the nearer seed; and an
// outline's glow over its width. The passes sample their images and call these with what they read.
//
// It is written in the subset of GLSL that GLM also compiles as C++, as clusters.glsl is: no
// swizzles, no out parameters, no struct constructors, float literals with an f suffix. The render
// suite runs it as C++ (render/PostProcess.h).

#ifndef JETTISON_POST_GLSL
#define JETTISON_POST_GLSL

// ---- Ambient obscurance --------------------------------------------------------------------------
//
// McGuire's Alchemy estimator as Scalable Ambient Obscurance has it (2011, 2012): each sample Q of the
// view-space surface within a radius of the pixel's P, in a spiral on the screen, occludes it as far
// as Q is above P's tangent plane, falling off with the square of their distance and to nothing at
// the radius. A flat surface occludes nothing, and an inside corner a share that grows towards it.

// The samples a pixel takes, and the turns of the spiral they lie on: a count prime to the pattern
// keeps two of them from lining up.
const int kAoSamples = 12;
const float kAoSpiralTurns = 7.0f;
// What a sample has to be above the tangent plane by, in metres per metre of view depth, before it
// occludes: it keeps a surface's own triangles from darkening it where the depth is quantised.
const float kAoBias = 0.002f;
const float kAoEpsilon = 0.01f;

// The view-space position of a pixel's centre at a reversed, infinite depth: the projection
// (Render::ReversedInfinitePerspective) puts a point at view z as depth near / -z, and its x and y on
// the screen at the projection's scale over -z, with y down.
vec3 ViewPositionAt(vec2 pixel, float depth, vec2 viewport, vec2 projectionScale, float nearPlane)
{
	float viewZ = -nearPlane / max(depth, 1e-30f);
	float ndcX = (pixel[0] + 0.5f) / viewport[0] * 2.0f - 1.0f;
	float ndcY = (pixel[1] + 0.5f) / viewport[1] * 2.0f - 1.0f;
	return vec3(ndcX * -viewZ / projectionScale[0], ndcY * -viewZ / projectionScale[1], viewZ);
}

// The pixels a view-space radius covers on the screen at a view depth: the projection's vertical
// scale is the focal length, in half the viewport's height.
float ScreenRadius(float radius, float viewDepth, float viewportHeight, float verticalScale)
{
	return radius * abs(verticalScale) * 0.5f * viewportHeight / max(viewDepth, 1e-6f);
}

// Where the i-th of a pixel's samples is, as a unit offset times the share of the screen radius it
// reaches: on a spiral of kAoSpiralTurns, turned by the pixel's own angle.
vec3 AoSpiralSample(int index, float angle)
{
	float along = (float(index) + 0.5f) / float(kAoSamples);
	float turn = along * (kAoSpiralTurns * 6.28318531f) + angle;
	return vec3(cos(turn), sin(turn), along);
}

// The angle a pixel turns its spiral by: one of sixteen, in a 4 x 4 pattern every pixel of which
// differs, so the blur over the same 4 x 4 takes the pattern out.
float AoPixelAngle(int x, int y)
{
	int cell = (x & 3) * 4 + (y & 3);
	// A permutation of the sixteen, so neighbours are far apart in angle.
	int order = (cell * 7 + 3) & 15;
	return float(order) * (6.28318531f / 16.0f);
}

// What one sample Q adds to the obscurance of P, whose normal is n, within the radius.
float ObscuranceSample(vec3 p, vec3 n, vec3 q, float radius)
{
	vec3 v = q - p;
	float vv = dot(v, v);
	float falloff = max(radius * radius - vv, 0.0f);
	float above = dot(v, n) - kAoBias * abs(p[2]);
	return falloff * falloff * falloff * max(above / (vv + kAoEpsilon), 0.0f);
}

// The share of the ambient light that reaches P, from the sum of its samples' obscurance: 1 with
// nothing about it, towards 0 deep in a corner. Intensity scales how dark the corners go.
float AmbientAccess(float sum, float samples, float radius, float intensity)
{
	float r2 = radius * radius;
	float r6 = r2 * r2 * r2;
	return max(1.0f - sum * intensity * 5.0f / (r6 * samples), 0.0f);
}

// The share of the environment's specular light that reaches a surface with an ambient access, by
// Lagarde and de Rousiers' fit (Moving Frostbite to PBR, 2014): a rough surface's lobe is as wide as
// the occlusion and takes all of it, and a smooth one's, looked at square on, slips past it.
float SpecularAccess(float nDotV, float access, float roughness)
{
	float exponent = exp2(-16.0f * roughness - 1.0f);
	return clamp(pow(nDotV + access, exponent) - 1.0f + access, 0.0f, 1.0f);
}

// How much a neighbour of the blur counts beside the pixel, by how far their view depths differ: a
// neighbour across an edge onto another surface does not, so the occlusion keeps to its surface.
const float kAoBlurDepthTolerance = 0.05f;

float AoBlurWeight(float depth, float neighbourDepth)
{
	return max(1.0f - abs(neighbourDepth - depth) / (kAoBlurDepthTolerance * depth), 0.0f);
}

// ---- Bloom ---------------------------------------------------------------------------------------
//
// Jimenez's chain (Next Generation Post Processing in Call of Duty: Advanced Warfare, 2014): each
// level is the one above it filtered down by thirteen bilinear taps, as five overlapping 4 x 4 boxes,
// and on the way back up each level adds the one below it, filtered up by a tent. The first level
// down weights each box by the inverse of its brightness (Karis), so a pixel far brighter than its
// neighbours, a star or a glint, spreads no more light than a few would.

// The levels below the frame, each half the one above; the last is a thirty-second of the frame.
const int kBloomLevels = 5;

// The taps as Jimenez lays them out, in texels of the level above:
//   a . b . c
//   . j . k .
//   d . e . f
//   . l . m .
//   g . h . i
// The middle box (j k l m) has half the weight, and the four corner boxes, each four of the outer
// taps with e, an eighth each.
float KarisWeight(vec3 colour)
{
	float luma = dot(colour, vec3(0.2126f, 0.7152f, 0.0722f));
	return 1.0f / (1.0f + luma);
}

vec3 BloomBox(vec3 a, vec3 b, vec3 c, vec3 d, float weight, bool karis)
{
	vec3 mean = (a + b + c + d) * 0.25f;
	if (karis)
		return mean * (weight * KarisWeight(mean));
	return mean * weight;
}

// The thirteen taps filtered down. With karis, each box is weighted by its brightness and the result
// divided by the weights' sum, so a flat image is kept as it is either way.
vec3 BloomDownsample(
    vec3 a, vec3 b, vec3 c, vec3 d, vec3 e, vec3 f, vec3 g, vec3 h, vec3 i, vec3 j, vec3 k, vec3 l,
    vec3 m, bool karis)
{
	vec3 sum = BloomBox(j, k, l, m, 0.5f, karis) + BloomBox(a, b, d, e, 0.125f, karis) +
	           BloomBox(b, c, e, f, 0.125f, karis) + BloomBox(d, e, g, h, 0.125f, karis) +
	           BloomBox(e, f, h, i, 0.125f, karis);
	if (!karis)
		return sum;
	float weights = 0.5f * KarisWeight((j + k + l + m) * 0.25f) +
	                0.125f * KarisWeight((a + b + d + e) * 0.25f) +
	                0.125f * KarisWeight((b + c + e + f) * 0.25f) +
	                0.125f * KarisWeight((d + e + g + h) * 0.25f) +
	                0.125f * KarisWeight((e + f + h + i) * 0.25f);
	return sum / weights;
}

// The tent the way up filters by, over nine taps a texel of the level below apart: the corners a
// sixteenth, the edges an eighth, the middle a quarter.
vec3 BloomUpsample(
    vec3 topLeft, vec3 top, vec3 topRight, vec3 left, vec3 middle, vec3 right, vec3 bottomLeft,
    vec3 bottom, vec3 bottomRight)
{
	return (topLeft + topRight + bottomLeft + bottomRight) * 0.0625f +
	       (top + left + right + bottom) * 0.125f + middle * 0.25f;
}

// The frame with its bloom: the top level holds every level's light added up, so its mean is it over
// the levels, and the frame keeps the rest of its light, so nothing is brighter for being blurred.
vec3 ApplyBloom(vec3 frame, vec3 bloomTop, float strength)
{
	return frame * (1.0f - strength) + bloomTop * (strength / float(kBloomLevels));
}

// ---- Outlines ------------------------------------------------------------------------------------
//
// The targeted are glows (Scene::OutlineStyle): the distance from each pixel to the nearest pixel of
// an outlined shape, by Rong and Tan's jump flood (2006). Each step looks at nine pixels a stride
// apart, the stride halving from 16 to 1, and keeps the nearest seed any of them knows of, so a seed
// reaches 31 pixels in five passes.

// A pixel that knows of no seed.
const uint kNoSeed = 65535u;
// The strides, longest first.
const int kFloodSteps = 5;

int FloodStride(int step)
{
	return 16 >> step;
}

// Of two seeds a pixel has been told of, the nearer to it, or the first where they are as near: the
// order the neighbours are looked at in is fixed, so the answer is the same every frame.
uvec2 NearerSeed(uvec2 pixel, uvec2 best, uvec2 candidate)
{
	if (candidate[0] == kNoSeed)
		return best;
	if (best[0] == kNoSeed)
		return candidate;
	vec2 toBest = vec2(float(best[0]) - float(pixel[0]), float(best[1]) - float(pixel[1]));
	vec2 toCandidate =
	    vec2(float(candidate[0]) - float(pixel[0]), float(candidate[1]) - float(pixel[1]));
	return dot(toCandidate, toCandidate) < dot(toBest, toBest) ? candidate : best;
}

// A glow's strength at a distance in pixels from its shape: full beside it, fading as a square to
// nothing at its width.
float GlowFalloff(float distance, float width)
{
	float left = max(1.0f - distance / max(width, 1.0f), 0.0f);
	return left * left;
}

// An object ID as the depth prepass writes it, a whole number in a float (exact to 2^24): the draw's
// index plus one in the low sixteen bits, 0 past the frame's 65535th, and its outline's index plus one
// in the eight above them, 0 for none.
uint ObjectOutline(float objectId)
{
	return (uint(objectId) >> 16u) & 255u;
}

// ---- Colour grading -----------------------------------------------------------------------------
//
// A grade's table is a 3D texture of its entries (render/ColourGrade.h), sampled with the colour on
// the display, encoded sRGB, after the tonemap.

// Where an encoded colour is looked up in a table of `size` entries a side: 0 and 1 at the centres of
// the first and last texels, so the device's trilinear filter interpolates between the entries, as
// Render::SampleLut does, and never into the clamp beyond them.
vec3 LutCoordinate(vec3 encoded, float size)
{
	return (clamp(encoded, 0.0f, 1.0f) * (size - 1.0f) + 0.5f) / size;
}

#endif
