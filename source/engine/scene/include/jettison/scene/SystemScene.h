#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Jettison::Celestial
{
struct StarSystem;
}

namespace Jettison::Scene
{
// Content layers within a system's scene: for loading in stages and for editor visibility.
enum class Layer : std::uint8_t
{
	Environment, // Bodies, stations, the skybox: what the system description places.
	Traffic,     // Ships, cargo, debris: what the simulation places.
	Effects      // Particles, beacons, transient visuals.
};

// The presentation scene for one star system, built from the system description and the
// simulation state, entered and left through jumps (docs/scope.md, "Scene: management"). The
// scene graph the engine renders today is parked in render until this exists
// (docs/architecture.md, "Parked code"). Filled in phase 6.
class SystemScene
{
  public:
	// Begins loading a system's content asynchronously; IsReady() turns true when it is in.
	void BeginLoad(const Celestial::StarSystem &system);
	bool IsReady() const
	{
		return m_ready;
	}
	void Unload();

	void SetLayerVisible(Layer layer, bool visible);
	bool IsLayerVisible(Layer layer) const;

  private:
	bool m_ready{false};
	[[maybe_unused]] std::uint8_t m_visibleLayers{0xFF};
};
} // namespace Jettison::Scene
