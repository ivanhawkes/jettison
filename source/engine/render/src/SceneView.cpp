#include <jettison/render/SceneView.h>

#include <jettison/scene/Components.h>

namespace Jettison::Render
{
CameraView ActiveCameraView(const entt::registry &registry)
{
	using Scene::CameraComponent;
	CameraView view;
	const CameraComponent fallback;
	const CameraComponent *camera = &fallback;
	entt::entity cameraEntity = entt::null;
	for (const entt::entity entity : registry.view<const CameraComponent>())
		if (registry.get<CameraComponent>(entity).active)
		{
			camera = &registry.get<CameraComponent>(entity);
			cameraEntity = entity;
			break;
		}
	view.verticalFov = glm::radians(camera->verticalFovDegrees);
	view.nearPlane = camera->nearPlane;
	if (const auto *pose = cameraEntity == entt::null
	                           ? nullptr
	                           : registry.try_get<Scene::CameraPoseComponent>(cameraEntity))
	{
		view.position = pose->position;
		view.orientation = pose->orientation;
	}
	else
	{
		view.position = camera->position;
		view.orientation =
		    OrientationFromAngles(camera->rotation.x, camera->rotation.y, camera->rotation.z);
	}
	// An orbit camera moves with what it circles, and a pilot's with the station the ship flies by,
	// which is where the shadows' grid is fixed. A pilot camera places the camera over its orbit rig.
	entt::entity frame = entt::null;
	if (cameraEntity != entt::null)
	{
		if (const auto *pilot = registry.try_get<Scene::PilotCameraComponent>(cameraEntity))
			frame = pilot->frame;
		else if (const auto *orbit = registry.try_get<Scene::OrbitCameraComponent>(cameraEntity))
			frame = orbit->target;
	}
	if (const auto *world =
	        registry.valid(frame) ? registry.try_get<Scene::WorldTransformComponent>(frame) : nullptr)
		view.frameOrigin = glm::dvec3{world->matrix[3]};
	return view;
}
} // namespace Jettison::Render
