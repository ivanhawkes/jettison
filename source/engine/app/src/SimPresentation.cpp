#include <jettison/app/SimPresentation.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <optional>

#include <jettison/scene/Components.h>

namespace Jettison::App
{
void SyncSimProxies(
    entt::registry &presentation, const Sim::Snapshot &previous, const Sim::Snapshot &current,
    double alpha)
{
	for (auto [entity, proxy, transform] :
	     presentation.view<const SimProxyComponent, Scene::TransformComponent>().each())
	{
		const std::optional<Sim::Transform> shown =
		    Sim::Interpolate(previous, current, proxy.simEntity, alpha);
		if (!shown)
			continue;
		transform.translation = shown->position;
		// The scene's rotation is Euler angles applied about x, then y, then z, which is the
		// order extractEulerAngleXYZ reads them back in.
		const glm::mat4 rotation = glm::mat4_cast(glm::quat{shown->orientation});
		glm::extractEulerAngleXYZ(
		    rotation, transform.rotation.x, transform.rotation.y, transform.rotation.z);
	}
}
} // namespace Jettison::App
