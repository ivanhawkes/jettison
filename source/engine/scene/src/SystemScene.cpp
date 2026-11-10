#include <jettison/scene/SystemScene.h>

#include <jettison/core/Assert.h>

namespace Jettison::Scene
{
void SystemScene::BeginLoad(const Celestial::StarSystem & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void SystemScene::Unload()
{
	JETTISON_NOT_IMPLEMENTED();
}
void SystemScene::SetLayerVisible(Layer /*unused*/, bool /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
bool SystemScene::IsLayerVisible(Layer /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Scene
