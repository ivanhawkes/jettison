#include "../src/Settings.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <jettison/input/Actions.h>

using namespace Jettison;
using Input::Binding;
using Input::Keys::Character;

namespace
{
const std::string kThrottleGuid = "030000004f04000004b6000000000000";

// The shell's input as a run sets it up, every run the same: its actions, contexts and defaults.
struct Shell
{
	Input::InputSystem input;
	Input::ActionId jump = input.DefineAction("jump_next", Input::ActionType::Button);
	Input::ActionId throttle = input.DefineAction("throttle", Input::ActionType::Axis);

	Shell()
	{
		input.DefineContext("shell");
		input.DefineContext("flight");
		input.Bind("shell", jump, {Character('j')});
		input.Bind("flight", throttle, {Character('w'), Character('s')});
	}
};

std::filesystem::path Scratch()
{
	const auto directory = std::filesystem::temp_directory_path() /
	                       ("jettison-settings-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(directory, error);
	return directory;
}

std::vector<std::string> Words(std::initializer_list<const char *> words)
{
	return {words.begin(), words.end()};
}
} // namespace

TEST_CASE("Settings: a rebinding survives a restart, and so does putting it back")
{
	const std::filesystem::path directory = Scratch();
	const std::filesystem::path file = directory / "config" / "settings.yaml";
	{
		Shell shell;
		App::Settings settings{file};
		settings.Load(shell.input);
		const App::BindResult result = App::Bind(shell.input, Words({"shell", "jump_next", "Key.K"}));
		CHECK(result.changed);
		CHECK(result.message == "shell.jump_next is Key.K");
		App::Bind(shell.input, Words({"flight", "throttle", "Joystick2.Axis2", "Key.Up/Key.Down"}));
		settings.Joysticks().Name(1, kThrottleGuid);
		settings.Save(shell.input);
	}
	CHECK(std::filesystem::exists(file));

	// The next run: the same setup, then the file.
	{
		Shell shell;
		App::Settings settings{file};
		settings.Load(shell.input);
		CHECK(shell.input.Bindings("shell", shell.jump) == std::vector<Binding>{{Character('k')}});
		CHECK(
		    shell.input.Bindings("flight", shell.throttle) ==
		    std::vector<Binding>{{Input::Joysticks::Axis(1, 2)}, {Input::Keys::Up, Input::Keys::Down}});
		CHECK(settings.Joysticks().NameOf(1) == kThrottleGuid);
		CHECK(
		    App::Bind(shell.input, Words({"shell", "jump_next"})).message ==
		    "shell.jump_next is Key.K");

		const App::BindResult reset = App::Unbind(shell.input, Words({"shell", "jump_next"}));
		CHECK(reset.message == "shell.jump_next is back to Key.J");
		settings.Save(shell.input);
	}
	{
		Shell shell;
		App::Settings settings{file};
		settings.Load(shell.input);
		CHECK(shell.input.Bindings("shell", shell.jump) == std::vector<Binding>{{Character('j')}});
		CHECK(shell.input.Bindings("flight", shell.throttle).size() == 2);
	}
	std::filesystem::remove_all(directory);
}

TEST_CASE("Settings: a file that does not read leaves the defaults, and a bind that is not is refused")
{
	const std::filesystem::path directory = Scratch();
	std::filesystem::create_directories(directory);
	const std::filesystem::path file = directory / "settings.yaml";
	std::ofstream{file}
	    << "input:\n  shell:\n    jump_next: Key.K\n  flight:\n    throttle: Key.Banana\n";

	Shell shell;
	App::Settings settings{file};
	settings.Load(shell.input);
	// Nothing of it applied, the good binding included.
	CHECK(shell.input.Bindings("shell", shell.jump) == std::vector<Binding>{{Character('j')}});

	CHECK_THROWS_WITH_AS(
	    App::Bind(shell.input, Words({"shell", "jump"})), "Input: there is no action 'jump'",
	    std::runtime_error);
	CHECK_THROWS_WITH_AS(
	    App::Bind(shell.input, Words({"map", "jump_next", "Key.K"})),
	    "Input: there is no context 'map'", std::runtime_error);
	CHECK_THROWS_WITH_AS(
	    App::Bind(shell.input, Words({"shell", "jump_next", "Key.Banana"})),
	    "'Key.Banana' is not bindings", std::runtime_error);
	CHECK_THROWS_AS(
	    App::Bind(shell.input, Words({"shell", "jump_next", "y:Key.K"})), std::runtime_error);
	CHECK_THROWS_AS(App::Bind(shell.input, Words({"shell"})), std::runtime_error);
	CHECK_THROWS_AS(
	    App::Unbind(shell.input, Words({"shell", "jump_next", "Key.K"})), std::runtime_error);
	CHECK(shell.input.Bindings("shell", shell.jump) == std::vector<Binding>{{Character('j')}});

	// The first save after it keeps the file that did not read beside the one it writes.
	App::Bind(shell.input, Words({"shell", "jump_next", "Key.L"}));
	settings.Save(shell.input);
	std::filesystem::path aside = file;
	aside += ".unread";
	REQUIRE(std::filesystem::exists(aside));
	std::ifstream kept{aside};
	const std::string text{std::istreambuf_iterator<char>{kept}, std::istreambuf_iterator<char>{}};
	CHECK(text.find("Key.Banana") != std::string::npos);
	Shell next;
	App::Settings again{file};
	again.Load(next.input);
	CHECK(next.input.Bindings("shell", next.jump) == std::vector<Binding>{{Character('l')}});
	std::filesystem::remove_all(directory);
}
