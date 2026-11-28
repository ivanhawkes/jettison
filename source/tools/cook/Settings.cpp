#include "Settings.h"

#include <format>
#include <stdexcept>
#include <vector>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>

namespace Jettison::Cook
{
namespace
{
constexpr Serialise::FormatVersion kSettingsVersion{1};

constexpr std::string_view kDefaultsName = "the engine's cook defaults";


void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<Geometry::LodSettings>("Cook.LodSettings")
		    .Field<&Geometry::LodSettings::maxLevels>("maxLevels", Attribute::Saved)
		    .Field<&Geometry::LodSettings::reduction>("reduction", Attribute::Saved)
		    .Field<&Geometry::LodSettings::maxError>("maxError", Attribute::Saved);
		RegisterType<TextureSettings>("Cook.TextureSettings")
		    .Field<&TextureSettings::quality>("quality", Attribute::Saved)
		    .Field<&TextureSettings::rdo>("rdo", Attribute::Saved)
		    .Field<&TextureSettings::zstdLevel>("zstdLevel", Attribute::Saved)
		    .Field<&TextureSettings::mipmaps>("mipmaps", Attribute::Saved);
		RegisterType<Settings>("Cook.Settings")
		    .Field<&Settings::lod>("lod", Attribute::Saved)
		    .Field<&Settings::textures>("textures", Attribute::Saved);
		return true;
	}();
	(void)registered;
}


// What is wrong with the settings, or empty. Checked here as well as by CookModel, which cannot know
// the layer that set them.
std::string Problem(const Settings &settings)
{
	if (!Geometry::IsValid(settings.lod))
		return "lod needs maxLevels of at least 1, reduction within (0, 1) and maxError of at least 0";
	const TextureSettings &textures = settings.textures;
	if (textures.quality > TextureSettings::kMaxQuality || textures.zstdLevel < 1 ||
	    textures.zstdLevel > TextureSettings::kMaxZstdLevel)
		return std::format(
		    "textures need a quality of 0 to {} and a zstdLevel of 1 to {}",
		    TextureSettings::kMaxQuality, TextureSettings::kMaxZstdLevel);
	return {};
}
} // namespace


SettingsLayer DefaultSettingsLayer()
{
	RegisterTypes();
	return {std::string{kDefaultsName}, Serialise::YamlArchive::Write(Settings{}, kSettingsVersion)};
}


Settings ReadSettings(std::span<const SettingsLayer> layers)
{
	RegisterTypes();
	std::vector<Serialise::YamlLayer> yaml;
	Settings settings;
	for (const SettingsLayer &layer : layers)
	{
		yaml.push_back({layer.name, layer.text});
		try
		{
			settings = Serialise::YamlArchive::ReadLayered<Settings>(yaml, kSettingsVersion);
		}
		catch (const std::exception &error)
		{
			throw std::runtime_error(std::format("Cook settings {}: {}", layer.name, error.what()));
		}
		if (const std::string problem = Problem(settings); !problem.empty())
			throw std::runtime_error(std::format("Cook settings {}: {}", layer.name, problem));
	}
	return settings;
}
} // namespace Jettison::Cook
