#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

#include <jettison/math/Easing.h>

namespace Jettison::Scene
{
// Where a camera rig puts the camera: what CameraPoseComponent holds.
struct CameraPose
{
	glm::dvec3 position{0.0};
	glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
};

// The rotation that points a camera's -Z along forward with its +Y towards up. Forward need not
// be normalised; an up parallel to it falls back to +Z, so a camera straight above the system
// plane still has a roll.
glm::dquat LookRotation(const glm::dvec3 &forward, const glm::dvec3 &up = {0.0, 1.0, 0.0});

// Exponential smoothing: the fraction of the way from current to goal a step of dt seconds
// covers at `rate` per second. 1 - e^(-rate dt), so ten steps of a tenth of a second land where
// one step of a second does, whatever the frame rate. A rate of zero or less snaps.
double SmoothingFactor(double rate, double dt);

// The rigs smooth what the player moves (angles, zoom, the lag of a chase) and follow what they
// are attached to exactly. A station at 7.7 km/s that the camera trailed by even one frame's
// smoothing would sit 130 metres off centre and swim, so the target's position is added in
// double precision, every frame, with no filter on it.

struct OrbitCameraSettings
{
	double minDistance{1.0};
	double maxDistance{1.0e12};
	// Short of straight up and down, where yaw would spin the view about its own axis.
	double maxPitch{1.5};
	double smoothing{12.0};
};

// Around a station or a planet: yaw and pitch about the target, and a distance that zooms by
// ratios, so a step means the same at ten metres and at ten thousand kilometres.
class OrbitCamera
{
  public:
	using Settings = OrbitCameraSettings;

	explicit OrbitCamera(Settings settings = {});

	void SetTarget(const glm::dvec3 &target)
	{
		m_target = target;
	}
	// Radians: yaw about +Y, pitch up from the target's horizon.
	void Orbit(double yaw, double pitch);
	// Multiplies the distance: 0.5 halves it.
	void Zoom(double factor);
	// Sets the goals and the smoothed values at once, so the camera starts there.
	void Place(double yaw, double pitch, double distance);

	double Distance() const
	{
		return m_distance;
	}
	CameraPose Update(double dt);

  private:
	Settings m_settings;
	glm::dvec3 m_target{0.0};
	double m_goalYaw{0.0};
	double m_goalPitch{0.3};
	double m_goalDistance{10.0};
	double m_yaw{0.0};
	double m_pitch{0.3};
	double m_distance{10.0};
};

struct ChaseCameraSettings
{
	glm::dvec3 offset{0.0, 4.0, 18.0};
	// How far ahead of the target, along its forward, the camera looks.
	double lookAhead{20.0};
	double smoothing{4.0};
};

// Behind a ship: the offset is in the target's frame (+Z behind, +Y above, as a ship flies along
// its -Z), and the rig's own rotation follows the target's with a lag, so a turn swings the
// camera round rather than snapping it. Only the rotation lags; the position is the target's
// plus the lagged offset, so speed alone never moves the ship in the frame.
class ChaseCamera
{
  public:
	using Settings = ChaseCameraSettings;

	explicit ChaseCamera(Settings settings = {});

	void SetTarget(const CameraPose &target)
	{
		m_target = target;
	}
	// Starts the lag where the target is now, so the first frame is not a swing.
	void Snap();
	CameraPose Update(double dt);

  private:
	Settings m_settings;
	CameraPose m_target;
	glm::dquat m_follow{1.0, 0.0, 0.0, 0.0};
	bool m_snapped{false};
};

struct CockpitCameraSettings
{
	glm::dvec3 eye{0.0, 0.8, -1.5};
	double maxYaw{2.0};
	double maxPitch{1.2};
	double smoothing{16.0};
};

// In the seat: rigid with the target, at an eye offset in its frame, with a free look the player
// turns and that is held inside a cone ahead.
class CockpitCamera
{
  public:
	using Settings = CockpitCameraSettings;

	explicit CockpitCamera(Settings settings = {});

	void SetTarget(const CameraPose &target)
	{
		m_target = target;
	}
	void Look(double yaw, double pitch);
	// Back to straight ahead.
	void Centre();
	CameraPose Update(double dt);

  private:
	Settings m_settings;
	CameraPose m_target;
	double m_goalYaw{0.0};
	double m_goalPitch{0.0};
	double m_yaw{0.0};
	double m_pitch{0.0};
};

struct MapCameraSettings
{
	double minDistance{1.0e3};
	double maxDistance{1.0e13};
	// Each zoom step multiplies or divides the distance by this.
	double zoomRatio{1.25};
	double minPitch{0.2};
	double maxPitch{1.55};
	double smoothing{10.0};
};

// Over the system (or, at phase 6, the galaxy): looking down at the reference plane from a
// distance that zooms logarithmically, from a station's approach to the whole system, about a
// focus that may follow a body, panned across the plane by an amount that scales with the
// distance.
class MapCamera
{
  public:
	using Settings = MapCameraSettings;

	explicit MapCamera(Settings settings = {});

	void SetFocus(const glm::dvec3 &focus)
	{
		m_focus = focus;
	}
	// Steps in, positive, or out, negative.
	void Zoom(double steps);
	// In units of the camera's distance from the focus, so a pan crosses the same share of the
	// view at every zoom.
	void Pan(double right, double forward);
	void Rotate(double yaw, double pitch);
	// Back on the focus.
	void Recentre();

	double Distance() const
	{
		return std::exp(m_logDistance);
	}
	CameraPose Update(double dt);

  private:
	Settings m_settings;
	glm::dvec3 m_focus{0.0};
	glm::dvec3 m_goalPan{0.0};
	glm::dvec3 m_pan{0.0};
	double m_goalLogDistance{0.0};
	double m_logDistance{0.0};
	double m_goalYaw{0.0};
	double m_yaw{0.0};
	double m_goalPitch{1.0};
	double m_pitch{1.0};
};

// A move from where one rig had the camera to where another has it, over a duration and along an
// easing curve: the position along the line in double precision, the orientation along the
// shorter arc. The destination is read each frame, so it may move during the transition.
class CameraTransition
{
  public:
	CameraTransition() = default;
	CameraTransition(
	    const CameraPose &from, double duration, Math::Easing easing = Math::Easing::InOutCubic);

	bool Active() const
	{
		return m_duration > 0.0 && m_elapsed < m_duration;
	}
	// Advances by dt and blends towards `to`; `to` itself once the transition is done.
	CameraPose Update(const CameraPose &to, double dt);

  private:
	CameraPose m_from;
	double m_duration{0.0};
	double m_elapsed{0.0};
	Math::Easing m_easing{Math::Easing::InOutCubic};
};
} // namespace Jettison::Scene
