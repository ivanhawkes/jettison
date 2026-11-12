#pragma once

#include <cstdint>

namespace Jettison::Scene
{
// The shape a value driven by presentation time follows over one period, from 0 to 1: Sine eases
// out and back, Triangle climbs and falls, Saw climbs and drops, Square holds each end for half the
// period. A material's animated parameters (render/Material.h) and a light's intensity
// (LightComponent) move by it, so a lamp's glow and its light blink together.
enum class Wave : std::uint8_t
{
	Sine,
	Triangle,
	Saw,
	Square,
};

// The wave at a point in its cycle (whole cycles are ignored), from 0 to 1.
float EvaluateWave(Wave wave, double cycles);

// Registers Wave for the files that name one, a scene's and a material's, under "Scene.Wave", once
// however often it is called.
void RegisterWave();
} // namespace Jettison::Scene
