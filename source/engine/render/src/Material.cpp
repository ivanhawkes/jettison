#include <jettison/render/Material.h>

#include <set>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>

namespace Jettison::Render
{
namespace
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterEnum<TextureFallback>("Render.TextureFallback")
		    .Value<TextureFallback::White>("White")
		    .Value<TextureFallback::Black>("Black")
		    .Value<TextureFallback::FlatNormal>("FlatNormal");
		RegisterEnum<ProgramBlend>("Render.ProgramBlend")
		    .Value<ProgramBlend::Opaque>("Opaque")
		    .Value<ProgramBlend::Transparent>("Transparent");
		Scene::RegisterWave();
		RegisterType<ProgramParameter>("Render.ProgramParameter")
		    .Field<&ProgramParameter::name>("name", Attribute::Saved)
		    .Field<&ProgramParameter::defaultValue>("default", Attribute::Saved);
		RegisterType<ProgramTexture>("Render.ProgramTexture")
		    .Field<&ProgramTexture::name>("name", Attribute::Saved)
		    .Field<&ProgramTexture::fallback>("fallback", Attribute::Saved);
		RegisterType<ProgramDefinition>("Render.ProgramDefinition")
		    .Field<&ProgramDefinition::blend>("blend", Attribute::Saved)
		    .Field<&ProgramDefinition::parameters>("parameters", Attribute::Saved)
		    .Field<&ProgramDefinition::textures>("textures", Attribute::Saved);
		RegisterType<MaterialParameter>("Render.MaterialParameter")
		    .Field<&MaterialParameter::name>("name", Attribute::Saved)
		    .Field<&MaterialParameter::value>("value", Attribute::Saved);
		RegisterType<MaterialTexture>("Render.MaterialTexture")
		    .Field<&MaterialTexture::slot>("slot", Attribute::Saved)
		    .Field<&MaterialTexture::path>("path", Attribute::Saved);
		RegisterType<AnimatedParameter>("Render.AnimatedParameter")
		    .Field<&AnimatedParameter::name>("name", Attribute::Saved)
		    .Field<&AnimatedParameter::wave>("wave", Attribute::Saved)
		    .Field<&AnimatedParameter::period>("period", Attribute::Saved)
		    .Field<&AnimatedParameter::phase>("phase", Attribute::Saved)
		    .Field<&AnimatedParameter::from>("from", Attribute::Saved)
		    .Field<&AnimatedParameter::to>("to", Attribute::Saved);
		RegisterType<MaterialDefinition>("Render.MaterialDefinition", Attribute::Moddable)
		    .Field<&MaterialDefinition::program>("program", Attribute::Saved)
		    .Field<&MaterialDefinition::parameters>("parameters", Attribute::Saved)
		    .Field<&MaterialDefinition::textures>("textures", Attribute::Saved)
		    .Field<&MaterialDefinition::animated>("animated", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

[[noreturn]] void Refuse(std::string_view kind, std::string_view name, const std::string &what)
{
	throw Serialise::FormatError(std::string{kind} + " '" + std::string{name} + "': " + what);
}

// Names that are given twice, in a list of things that carry one.
template <typename T, typename Name>
void RefuseRepeats(
    const std::vector<T> &items, Name name, std::string_view kind, std::string_view file,
    std::string_view what)
{
	std::set<std::string_view> seen;
	for (const T &item : items)
		if (!seen.insert(name(item)).second)
			Refuse(kind, file, std::string{what} + " '" + std::string{name(item)} + "' is given twice");
}

template <typename T, typename Name>
std::size_t IndexOf(const std::vector<T> &items, std::string_view wanted, Name name)
{
	for (std::size_t index = 0; index < items.size(); ++index)
		if (name(items[index]) == wanted)
			return index;
	return items.size();
}
} // namespace


ProgramDefinition ReadProgram(std::string_view text, std::string_view name)
{
	RegisterTypes();
	auto program = Serialise::YamlArchive::Read<ProgramDefinition>(
	    text, Serialise::FormatVersion{kProgramFormatVersion});
	if (program.parameters.size() > kMaxMaterialParameters)
		Refuse(
		    "Program", name, "has more than " + std::to_string(kMaxMaterialParameters) + " parameters");
	if (program.textures.size() > kMaterialTextureSlots)
		Refuse(
		    "Program", name,
		    "has more than " + std::to_string(kMaterialTextureSlots) + " texture slots");
	RefuseRepeats(
	    program.parameters, [](const ProgramParameter &p) -> std::string_view { return p.name; },
	    "Program", name, "parameter");
	RefuseRepeats(
	    program.textures, [](const ProgramTexture &t) -> std::string_view { return t.name; }, "Program",
	    name, "texture slot");
	return program;
}

std::string WriteProgram(const ProgramDefinition &program)
{
	RegisterTypes();
	return Serialise::YamlArchive::Write(program, Serialise::FormatVersion{kProgramFormatVersion});
}

MaterialDefinition ReadMaterial(std::string_view text, std::string_view name)
{
	RegisterTypes();
	auto material = Serialise::YamlArchive::Read<MaterialDefinition>(
	    text, Serialise::FormatVersion{kMaterialFormatVersion});
	if (material.program.empty())
		Refuse("Material", name, "names no program");
	RefuseRepeats(
	    material.parameters, [](const MaterialParameter &p) -> std::string_view { return p.name; },
	    "Material", name, "parameter");
	RefuseRepeats(
	    material.textures, [](const MaterialTexture &t) -> std::string_view { return t.slot; },
	    "Material", name, "texture slot");
	RefuseRepeats(
	    material.animated, [](const AnimatedParameter &a) -> std::string_view { return a.name; },
	    "Material", name, "animated parameter");
	for (const AnimatedParameter &animated : material.animated)
		if (!std::isfinite(animated.period) || animated.period <= 0.0 || !std::isfinite(animated.phase))
			Refuse(
			    "Material", name,
			    "animated parameter '" + animated.name + "' needs a period above zero");
	for (const MaterialTexture &texture : material.textures)
		if (texture.path.empty())
			Refuse("Material", name, "texture slot '" + texture.slot + "' names no file");
	return material;
}

std::string WriteMaterial(const MaterialDefinition &material)
{
	RegisterTypes();
	return Serialise::YamlArchive::Write(material, Serialise::FormatVersion{kMaterialFormatVersion});
}


ResolvedMaterial ResolveDefaults(const ProgramDefinition &program)
{
	ResolvedMaterial resolved;
	for (const ProgramParameter &parameter : program.parameters)
		resolved.parameters.push_back(parameter.defaultValue);
	for (std::size_t binding = 0; binding < program.textures.size(); ++binding)
		resolved.fallbacks[binding] = program.textures[binding].fallback;
	return resolved;
}

ResolvedMaterial ResolveMaterial(
    const ProgramDefinition &program, const MaterialDefinition &material, std::string_view name)
{
	ResolvedMaterial resolved = ResolveDefaults(program);
	const auto parameterName = [](const ProgramParameter &p) -> std::string_view { return p.name; };
	const auto parameterIndex = [&](const std::string &wanted)
	{
		const std::size_t index = IndexOf(program.parameters, wanted, parameterName);
		if (index == program.parameters.size())
			Refuse(
			    "Material", name,
			    "program '" + material.program + "' has no parameter '" + wanted + "'");
		return index;
	};
	for (const MaterialParameter &parameter : material.parameters)
		resolved.parameters[parameterIndex(parameter.name)] = parameter.value;
	for (const AnimatedParameter &animated : material.animated)
		resolved.animations.push_back(
		    {parameterIndex(animated.name), animated.wave, animated.period, animated.phase,
		     animated.from, animated.to});
	for (const MaterialTexture &texture : material.textures)
	{
		const std::size_t binding = IndexOf(
		    program.textures, texture.slot,
		    [](const ProgramTexture &t) -> std::string_view { return t.name; });
		if (binding == program.textures.size())
			Refuse(
			    "Material", name,
			    "program '" + material.program + "' has no texture slot '" + texture.slot + "'");
		resolved.textures[binding] = texture.path;
	}
	return resolved;
}


void EvaluateParameters(const ResolvedMaterial &material, double time, std::vector<glm::vec4> &out)
{
	const std::size_t first = out.size();
	out.insert(out.end(), material.parameters.begin(), material.parameters.end());
	for (const ResolvedMaterial::Animation &animation : material.animations)
	{
		const float at = EvaluateWave(animation.wave, time / animation.period + animation.phase);
		out[first + animation.parameter] = glm::mix(animation.from, animation.to, at);
	}
}
} // namespace Jettison::Render
