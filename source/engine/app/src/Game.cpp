#include <jettison/app/Game.h>

namespace Jettison::App
{
void Game::Register(const ShellWorld & /*world*/, Shell & /*shell*/)
{
}


void Game::LoadDefinitions(Sim::DefinitionRegistry & /*definitions*/)
{
}


void Game::Define(const ShellWorld & /*world*/, const Sim::DefinitionRegistry & /*definitions*/)
{
}


void Game::Loaded(Shell & /*shell*/)
{
}


void Game::AddCommands(DevTools::Console & /*console*/, Shell & /*shell*/)
{
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


entt::entity Game::Followed() const
{
	return entt::null;
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

} // namespace Jettison::App
