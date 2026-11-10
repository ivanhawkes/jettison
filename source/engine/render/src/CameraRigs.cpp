#include <jettison/render/CameraRigs.h>

#include <algorithm>
#include <cmath>

namespace Jettison::Render
{
namespace
{
// From the target towards the camera, at a yaw about +Y and a pitch above the horizon.
glm::dvec3 Direction(double yaw, double pitch)
{
	return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

double Approach(double current, double goal, double rate, double dt)
{
	return current + (goal - current) * SmoothingFactor(rate, dt);
}
} // namespace

glm::dquat LookRotation(const glm::dvec3 &forward, const glm::dvec3 &up)
{
	const glm::dvec3 back = -glm::normalize(forward);
	glm::dvec3 right = glm::cross(up, back);
	if (glm::length(right) < 1e-9)
		right = glm::cross(glm::dvec3{0.0, 0.0, 1.0}, back);
	right = glm::normalize(right);
	const glm::dvec3 trueUp = glm::cross(back, right);
	return glm::quat_cast(glm::dmat3{right, trueUp, back});
}

double SmoothingFactor(double rate, double dt)
{
	if (rate <= 0.0)
		return 1.0;
	return 1.0 - std::exp(-rate * std::max(dt, 0.0));
}

// ---- Orbit -------------------------------------------------------------------------------------

OrbitCamera::OrbitCamera(Settings settings) : m_settings{settings}
{
	m_goalDistance = m_distance =
	    std::clamp(m_distance, m_settings.minDistance, m_settings.maxDistance);
}

void OrbitCamera::Orbit(double yaw, double pitch)
{
	m_goalYaw += yaw;
	m_goalPitch = std::clamp(m_goalPitch + pitch, -m_settings.maxPitch, m_settings.maxPitch);
}

void OrbitCamera::Zoom(double factor)
{
	if (factor > 0.0)
		m_goalDistance =
		    std::clamp(m_goalDistance * factor, m_settings.minDistance, m_settings.maxDistance);
}

void OrbitCamera::Place(double yaw, double pitch, double distance)
{
	m_goalYaw = m_yaw = yaw;
	m_goalPitch = m_pitch = std::clamp(pitch, -m_settings.maxPitch, m_settings.maxPitch);
	m_goalDistance = m_distance = std::clamp(distance, m_settings.minDistance, m_settings.maxDistance);
}

CameraPose OrbitCamera::Update(double dt)
{
	m_yaw = Approach(m_yaw, m_goalYaw, m_settings.smoothing, dt);
	m_pitch = Approach(m_pitch, m_goalPitch, m_settings.smoothing, dt);
	// In the logarithm, so zooming in from far away does not cover most of the way in one frame.
	m_distance =
	    std::exp(Approach(std::log(m_distance), std::log(m_goalDistance), m_settings.smoothing, dt));
	const glm::dvec3 offset = Direction(m_yaw, m_pitch) * m_distance;
	return {m_target + offset, LookRotation(-offset)};
}

// ---- Chase -------------------------------------------------------------------------------------

ChaseCamera::ChaseCamera(Settings settings) : m_settings{settings}
{
}

void ChaseCamera::Snap()
{
	m_follow = m_target.orientation;
	m_snapped = true;
}

CameraPose ChaseCamera::Update(double dt)
{
	if (!m_snapped)
		Snap();
	m_follow = glm::normalize(
	    glm::slerp(m_follow, m_target.orientation, SmoothingFactor(m_settings.smoothing, dt)));
	const glm::dvec3 position = m_target.position + m_follow * m_settings.offset;
	const glm::dvec3 aim =
	    m_target.position + m_target.orientation * glm::dvec3{0.0, 0.0, -m_settings.lookAhead};
	return {position, LookRotation(aim - position, m_follow * glm::dvec3{0.0, 1.0, 0.0})};
}

// ---- Cockpit -----------------------------------------------------------------------------------

CockpitCamera::CockpitCamera(Settings settings) : m_settings{settings}
{
}

void CockpitCamera::Look(double yaw, double pitch)
{
	m_goalYaw = std::clamp(m_goalYaw + yaw, -m_settings.maxYaw, m_settings.maxYaw);
	m_goalPitch = std::clamp(m_goalPitch + pitch, -m_settings.maxPitch, m_settings.maxPitch);
}

void CockpitCamera::Centre()
{
	m_goalYaw = 0.0;
	m_goalPitch = 0.0;
}

CameraPose CockpitCamera::Update(double dt)
{
	m_yaw = Approach(m_yaw, m_goalYaw, m_settings.smoothing, dt);
	m_pitch = Approach(m_pitch, m_goalPitch, m_settings.smoothing, dt);
	const glm::dquat look = glm::angleAxis(m_yaw, glm::dvec3{0.0, 1.0, 0.0}) *
	                        glm::angleAxis(m_pitch, glm::dvec3{1.0, 0.0, 0.0});
	return {m_target.position + m_target.orientation * m_settings.eye, m_target.orientation * look};
}

// ---- Map ---------------------------------------------------------------------------------------

MapCamera::MapCamera(Settings settings) : m_settings{settings}
{
	m_goalLogDistance = m_logDistance =
	    std::log(std::sqrt(m_settings.minDistance * m_settings.maxDistance));
}

void MapCamera::Zoom(double steps)
{
	m_goalLogDistance = std::clamp(
	    m_goalLogDistance - steps * std::log(m_settings.zoomRatio), std::log(m_settings.minDistance),
	    std::log(m_settings.maxDistance));
}

void MapCamera::Pan(double right, double forward)
{
	// Across the reference plane, turned by the map's yaw, scaled by how much of it is in view.
	const double scale = std::exp(m_goalLogDistance);
	const glm::dvec3 across{std::cos(m_goalYaw), 0.0, -std::sin(m_goalYaw)};
	const glm::dvec3 ahead{-std::sin(m_goalYaw), 0.0, -std::cos(m_goalYaw)};
	m_goalPan += (across * right + ahead * forward) * scale;
}

void MapCamera::Rotate(double yaw, double pitch)
{
	m_goalYaw += yaw;
	m_goalPitch = std::clamp(m_goalPitch + pitch, m_settings.minPitch, m_settings.maxPitch);
}

void MapCamera::Recentre()
{
	m_goalPan = glm::dvec3{0.0};
}

CameraPose MapCamera::Update(double dt)
{
	const double factor = SmoothingFactor(m_settings.smoothing, dt);
	m_logDistance += (m_goalLogDistance - m_logDistance) * factor;
	m_pan += (m_goalPan - m_pan) * factor;
	m_yaw = Approach(m_yaw, m_goalYaw, m_settings.smoothing, dt);
	m_pitch = Approach(m_pitch, m_goalPitch, m_settings.smoothing, dt);
	const glm::dvec3 centre = m_focus + m_pan;
	const glm::dvec3 offset = Direction(m_yaw, m_pitch) * std::exp(m_logDistance);
	return {centre + offset, LookRotation(-offset)};
}

// ---- Transition --------------------------------------------------------------------------------

CameraTransition::CameraTransition(const CameraPose &from, double duration, Math::Easing easing)
    : m_from{from}, m_duration{duration}, m_easing{easing}
{
}

CameraPose CameraTransition::Update(const CameraPose &to, double dt)
{
	if (!Active())
		return to;
	m_elapsed = std::min(m_elapsed + std::max(dt, 0.0), m_duration);
	const double t = Math::Ease(m_easing, static_cast<float>(m_elapsed / m_duration));
	return {
	    m_from.position + (to.position - m_from.position) * t,
	    glm::normalize(glm::slerp(m_from.orientation, to.orientation, t))};
}
} // namespace Jettison::Render
