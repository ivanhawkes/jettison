#include <jettison/app/Game.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/sim/Events.h>

namespace Jettison::App
{
void Game::Register(Shell & /*shell*/)
{
}


void Game::Loaded(Shell & /*shell*/)
{
}


void Game::AddCommands(DevTools::Console & /*console*/, Shell & /*shell*/)
{
}


std::string_view Game::DefaultScene() const
{
	return "scene/main.yaml";
}


void Game::RegisterFrame(Ecs::Scheduler & /*frame*/, Shell & /*shell*/)
{
}


void Game::SceneLoaded(Shell & /*shell*/)
{
}


void Game::SceneUnloading(Shell & /*shell*/)
{
}


bool Game::SyncProxies(Shell & /*shell*/)
{
	return false;
}


void Game::ShuttingDown(Shell & /*shell*/) noexcept
{
}


void Game::WriteSave(Sim::SaveGame & /*save*/, Shell & /*shell*/)
{
}


void Game::ReadSave(const Sim::SaveGame & /*save*/, Shell & /*shell*/)
{
}


void Game::OpenUi(Ui::UiSystem & /*ui*/, Shell & /*shell*/)
{
}


void Game::PollInput(const Input::InputSystem & /*input*/, Shell & /*shell*/)
{
}


bool Game::Back(Shell & /*shell*/)
{
	return false;
}


void Game::UpdateUi(Shell & /*shell*/)
{
}


void Game::DrawPanels(Shell & /*shell*/, bool /*devTools*/)
{
}


void Game::TakeEvent(const Sim::SimEvent & /*event*/, Shell & /*shell*/)
{
}


bool Game::Hears(const Sim::SimEvent & /*event*/) const
{
	return false;
}


std::optional<std::string> Game::Describe(entt::entity /*entity*/) const
{
	return std::nullopt;
}


std::optional<PickSpace> Game::PickIn(const Shell & /*shell*/) const
{
	return std::nullopt;
}


glm::dvec3 Game::PickOrigin(const Shell & /*shell*/, double /*simSeconds*/) const
{
	return glm::dvec3{0.0};
}


std::optional<std::string> Game::NameEntity(
    const Sim::Snapshot & /*snapshot*/, entt::entity /*entity*/) const
{
	return std::nullopt;
}


std::optional<std::string> Game::NameIdentifier(
    std::string_view /*definition*/, const std::string & /*identifier*/,
    const Sim::DefinitionRegistry & /*definitions*/, const Ui::StringTable & /*strings*/) const
{
	return std::nullopt;
}


void Game::DriveSmoke(SmokeActions & /*actions*/, Shell & /*shell*/)
{
}


entt::entity EventEntity(const Sim::SimEvent &event, std::string_view field)
{
	const entt::meta_type type = event.event.type();
	const entt::meta_data data = type ? Reflect::FindField(type, field) : entt::meta_data{};
	if (!data)
		return entt::null;
	const entt::meta_any value = data.get(event.event);
	const auto *entity = value.try_cast<entt::entity>();
	return entity != nullptr ? *entity : entt::entity{entt::null};
}
} // namespace Jettison::App
