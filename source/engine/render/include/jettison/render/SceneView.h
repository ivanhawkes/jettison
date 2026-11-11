#pragma once

#include <entt/entt.hpp>

#include <jettison/render/Camera.h>

namespace Jettison::Render
{
// The active camera's view of a scene this frame (scene/Components.h): its pose from
// CameraPoseComponent when a rig wrote one, otherwise from the authored CameraComponent, and the
// frame origin an orbit camera's target gives it. The default view when the scene has no active
// camera.
CameraView ActiveCameraView(const entt::registry &registry);
} // namespace Jettison::Render
