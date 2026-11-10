#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Render
{
// Materials are data (docs/architecture.md, "Rendering"): a file names a shader program, sets its
// parameters, fills its texture slots, and may drive parameters from time. The program says what
// it takes in a descriptor beside its SPIR-V, shaders/<program>.program.yaml, so a material is
// checked against the program it names when it loads, not when it draws.
//
// On the device every scene program shares one layout: set 1 holds kMaterialTextureSlots combined
// image samplers, in the program's slot order, and a material's parameters are vec4s in the
// frame's material buffer, in the program's parameter order.
constexpr std::size_t kMaterialTextureSlots = 4;
constexpr std::size_t kMaxMaterialParameters = 8;
constexpr std::uint32_t kMaterialFormatVersion = 1;
constexpr std::uint32_t kProgramFormatVersion = 1;

// What an empty texture slot samples: white for a colour multiplied in, black for one added, a
// flat tangent-space normal for a normal map.
enum class TextureFallback : std::uint8_t
{
	White,
	Black,
	FlatNormal,
};

struct ProgramParameter
{
	std::string name;
	glm::vec4 defaultValue{0.0f};
};

struct ProgramTexture
{
	std::string name;
	TextureFallback fallback{TextureFallback::White};
};

// shaders/<program>.program.yaml.
struct ProgramDefinition
{
	std::vector<ProgramParameter> parameters;
	std::vector<ProgramTexture> textures;
};

// The shape a time-driven parameter follows over one period, from 0 to 1: Sine eases out and back,
// Triangle climbs and falls, Saw climbs and drops, Square holds each end for half the period.
enum class Wave : std::uint8_t
{
	Sine,
	Triangle,
	Saw,
	Square,
};

struct MaterialParameter
{
	std::string name;
	glm::vec4 value{0.0f};
};

struct MaterialTexture
{
	std::string slot;
	// A virtual path (assets/, through the VFS).
	std::string path;
};

// A parameter that moves between two values with presentation time. It replaces any static value
// the material gives the same parameter.
struct AnimatedParameter
{
	std::string name;
	Wave wave{Wave::Sine};
	// Seconds per cycle, above zero.
	double period{1.0};
	// Where in its cycle the wave starts, in cycles.
	double phase{0.0};
	glm::vec4 from{0.0f};
	glm::vec4 to{1.0f};
};

// materials/<name>.material.yaml.
struct MaterialDefinition
{
	std::string program;
	std::vector<MaterialParameter> parameters;
	std::vector<MaterialTexture> textures;
	std::vector<AnimatedParameter> animated;
};

// Read and written through Serialise::YamlArchive; reading also checks what the format alone does
// not: a program is named, names are not repeated, periods are positive, and a program fits the
// device layout. Throws Serialise::FormatError naming the file.
ProgramDefinition ReadProgram(std::string_view text, std::string_view name);
std::string WriteProgram(const ProgramDefinition &program);
MaterialDefinition ReadMaterial(std::string_view text, std::string_view name);
std::string WriteMaterial(const MaterialDefinition &material);

// The wave at a point in its cycle (whole cycles are ignored), from 0 to 1.
float EvaluateWave(Wave wave, double cycles);

// A material bound to its program: everything a draw needs, by binding and by parameter index.
struct ResolvedMaterial
{
	struct Animation
	{
		std::size_t parameter{0};
		Wave wave{Wave::Sine};
		double period{1.0};
		double phase{0.0};
		glm::vec4 from{0.0f};
		glm::vec4 to{1.0f};
	};

	// The program's parameters, in its order: the material's value, or the program's default.
	std::vector<glm::vec4> parameters;
	std::vector<Animation> animations;
	// Per binding, the texture's virtual path, or empty for the slot's fallback.
	std::array<std::string, kMaterialTextureSlots> textures;
	std::array<TextureFallback, kMaterialTextureSlots> fallbacks{};
};

// The program's defaults, for a draw that names no material.
ResolvedMaterial ResolveDefaults(const ProgramDefinition &program);
// Throws Serialise::FormatError for a parameter or texture slot the program does not have.
ResolvedMaterial ResolveMaterial(
    const ProgramDefinition &program, const MaterialDefinition &material, std::string_view name);

// The material's parameters at a presentation time in seconds, appended to `out`. The time is a
// double, and the wave is reduced to its cycle in double before anything is a float, so a session
// hours long moves as smoothly as its first minute.
void EvaluateParameters(const ResolvedMaterial &material, double time, std::vector<glm::vec4> &out);
} // namespace Jettison::Render
