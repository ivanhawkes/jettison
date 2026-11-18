#include <jettison/app/Game.h>

namespace Jettison::App
{
void Game::Register(const ShellWorld & /*world*/, Input::InputSystem & /*input*/)
{
}


void Game::LoadDefinitions(Sim::DefinitionRegistry & /*definitions*/)
{
}


void Game::Define(const ShellWorld & /*world*/, const Sim::DefinitionRegistry & /*definitions*/)
{
}


void Game::OpenGalaxy(
    const ShellWorld & /*world*/, std::span<const Celestial::StarSystem> /*systems*/,
    const Sim::DefinitionRegistry & /*definitions*/, Script::ScriptHost & /*scripts*/)
{
}


void Game::StationOpened(
    Sim::SimWorld & /*world*/, Dock::Docks & /*docks*/, entt::entity /*station*/,
    std::string_view /*body*/)
{
}


void Game::StationsOpened(const ShellWorld & /*world*/, const Sim::DefinitionRegistry & /*definitions*/)
{
}


std::optional<ShellShip> Game::StarterShip() const
{
	return std::nullopt;
}


void Game::FitShip(
    entt::registry & /*world*/, entt::entity /*ship*/, const Sim::Controller & /*player*/)
{
}


void Game::Loaded(entt::registry & /*world*/, entt::entity /*ship*/, const Sim::Controller & /*player*/)
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


std::string Game::DescribeShip(
    const Sim::Snapshot & /*snapshot*/, entt::entity /*ship*/, bool atHelm) const
{
	return atHelm ? "at your helm" : "left to its behaviour";
}


void Game::DriveSmoke(SmokeActions & /*actions*/, Shell & /*shell*/)
{
}

} // namespace Jettison::App
