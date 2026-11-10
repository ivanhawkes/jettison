#include <jettison/core/Config.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unistd.h>

using namespace Jettison::Core;

namespace
{
// A directory of its own per test run, removed when the fixture goes.
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-config-tests-" + std::to_string(getpid()))}
	{
		std::filesystem::create_directories(m_directory);
	}
	~Scratch()
	{
		std::error_code error;
		std::filesystem::remove_all(m_directory, error);
	}
	Scratch(const Scratch &) = delete;
	Scratch &operator=(const Scratch &) = delete;

	std::filesystem::path Write(const std::string &name, const std::string &contents) const
	{
		const auto path = m_directory / name;
		std::ofstream{path} << contents;
		return path;
	}
	std::filesystem::path Path(const std::string &name) const
	{
		return m_directory / name;
	}

  private:
	std::filesystem::path m_directory;
};

std::string ReadFile(const std::filesystem::path &path)
{
	std::ifstream file{path};
	return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
} // namespace

TEST_CASE("Config: the command line overrides the user layer, which overrides the defaults")
{
	Config config;
	config.SetNumber(ConfigLayer::Defaults, "audio.music", 0.8);
	config.SetBool(ConfigLayer::Defaults, "render.vsync", true);
	config.SetBool(ConfigLayer::User, "render.vsync", false);
	config.SetBool(ConfigLayer::CommandLine, "render.vsync", true);
	config.Set(ConfigLayer::User, "player.name", "Ivan");

	CHECK(config.GetNumber("audio.music") == 0.8);
	CHECK(config.GetBool("render.vsync") == true);
	CHECK(config.GetString("player.name") == "Ivan");
	CHECK_FALSE(config.GetString("missing.key").has_value());
}

TEST_CASE("Config: a value that does not read as the type asked for falls through to the next layer")
{
	Config config;
	config.SetNumber(ConfigLayer::Defaults, "audio.music", 0.5);
	config.Set(ConfigLayer::User, "audio.music", "loud");
	CHECK(config.GetNumber("audio.music") == 0.5);
	CHECK(config.GetString("audio.music") == "loud");

	config.Set(ConfigLayer::User, "render.vsync", "yes");
	CHECK_FALSE(config.GetBool("render.vsync").has_value());
	config.Set(ConfigLayer::User, "audio.volume", "0.5x");
	CHECK_FALSE(config.GetNumber("audio.volume").has_value());
}

TEST_CASE("Config: keys are dotted segments, and none is both a value and a map")
{
	Config config;
	CHECK_THROWS_AS(config.Set(ConfigLayer::User, "", "x"), std::invalid_argument);
	CHECK_THROWS_AS(config.Set(ConfigLayer::User, "render.", "x"), std::invalid_argument);
	CHECK_THROWS_AS(config.Set(ConfigLayer::User, ".render", "x"), std::invalid_argument);
	CHECK_THROWS_AS(config.Set(ConfigLayer::User, "render..vsync", "x"), std::invalid_argument);
	CHECK_THROWS_AS(config.Set(ConfigLayer::User, "render vsync", "x"), std::invalid_argument);

	config.Set(ConfigLayer::User, "render.vsync", "true");
	CHECK_THROWS_AS(config.Set(ConfigLayer::User, "render", "x"), std::invalid_argument);
	CHECK_THROWS_AS(config.Set(ConfigLayer::User, "render.vsync.mode", "x"), std::invalid_argument);
	// A sibling that shares the first characters of a segment is a different key.
	config.Set(ConfigLayer::User, "renderer", "vulkan");
	config.Set(ConfigLayer::User, "render.vsync", "false");
	CHECK(config.GetBool("render.vsync") == false);
	// Layers are separate files, so the rule holds within one layer.
	config.Set(ConfigLayer::Defaults, "render", "x");
}

TEST_CASE("Config: a nested YAML file loads as dotted keys")
{
	const Scratch scratch;
	const auto path = scratch.Write(
	    "settings.yaml",
	    "render:\n  vsync: false\n  window:\n    width: 1280\naudio:\n  music: 0.25\n");
	Config config;
	config.LoadLayer(ConfigLayer::User, path);
	CHECK(config.GetBool("render.vsync") == false);
	CHECK(config.GetNumber("render.window.width") == 1280.0);
	CHECK(config.GetNumber("audio.music") == 0.25);
}

TEST_CASE("Config: a missing or empty file is an empty layer")
{
	const Scratch scratch;
	Config config;
	config.LoadLayer(ConfigLayer::User, scratch.Path("missing.yaml"));
	config.LoadLayer(ConfigLayer::User, scratch.Write("empty.yaml", ""));
	CHECK_FALSE(config.GetString("render.vsync").has_value());
}

TEST_CASE("Config: a malformed file throws naming the path, and leaves the layer as it was")
{
	const Scratch scratch;
	Config config;
	config.Set(ConfigLayer::User, "render.vsync", "true");

	const auto check = [&](const std::string &name, const std::string &contents)
	{
		const auto path = scratch.Write(name, contents);
		try
		{
			config.LoadLayer(ConfigLayer::User, path);
			FAIL("loaded ", name);
		}
		catch (const std::runtime_error &error)
		{
			CHECK(std::string{error.what()}.find(name) != std::string::npos);
		}
		CHECK(config.GetBool("render.vsync") == true);
		CHECK_FALSE(config.GetString("audio.music").has_value());
	};
	check("syntax.yaml", "audio:\n  music: 0.5\nrender: [unclosed\n");
	check("list.yaml", "audio:\n  music: 0.5\n- item\n");
	check("sequence.yaml", "audio:\n  music: 0.5\nrender:\n  modes: [a, b]\n");
	check("dotted.yaml", "audio:\n  music: 0.5\nrender.vsync: false\n");
	check("conflict.yaml", "audio:\n  music: 0.5\nrender:\n  vsync:\n    mode: on\n");
	check("null.yaml", "audio:\n  music: 0.5\nrender:\n  vsync:\n");
}

TEST_CASE(
    "Config: the user layer round-trips through its file, without the values equal to the defaults")
{
	const Scratch scratch;
	Config saved;
	saved.SetNumber(ConfigLayer::Defaults, "audio.music", 0.8);
	saved.SetNumber(ConfigLayer::User, "audio.music", 0.8);
	saved.SetNumber(ConfigLayer::User, "audio.effects", 0.1);
	saved.SetBool(ConfigLayer::User, "render.vsync", false);
	saved.SetNumber(ConfigLayer::User, "render.window.width", 1280);
	saved.SetNumber(ConfigLayer::User, "render.window.height", 720);
	saved.Set(ConfigLayer::User, "renderer", "vulkan");
	saved.Set(ConfigLayer::User, "player.name", "");
	saved.Set(ConfigLayer::User, "player.motto", "null: # not a comment");
	saved.Set(ConfigLayer::User, "player.title", "true");
	saved.Set(ConfigLayer::CommandLine, "render.vsync", "true");

	const auto path = scratch.Path("user.yaml");
	saved.SaveUserLayer(path);
	CAPTURE(ReadFile(path));
	CHECK(ReadFile(path).find("music") == std::string::npos);

	Config loaded;
	loaded.LoadLayer(ConfigLayer::User, path);
	for (const char *key :
	     {"audio.effects", "render.vsync", "render.window.width", "render.window.height", "renderer",
	      "player.name", "player.motto", "player.title"})
	{
		CAPTURE(key);
		const auto expected = saved.GetString(key);
		if (std::string_view{key} == "render.vsync")
			CHECK(loaded.GetString(key) == "false"); // The command line is not the user's file.
		else
			CHECK(loaded.GetString(key) == expected);
	}
	CHECK_FALSE(loaded.GetString("audio.music").has_value());
	CHECK(loaded.GetNumber("audio.effects") == 0.1);

	// Saving what was loaded writes the same file: the writer and the loader agree.
	const auto again = scratch.Path("again.yaml");
	loaded.SaveUserLayer(again);
	CHECK(ReadFile(again) == ReadFile(path));
}
