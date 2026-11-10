#include <jettison/render/Material.h>

#include <jettison/serialise/Archive.h>

#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Jettison::Render;
using Jettison::Serialise::FormatError;

namespace
{
std::string ReadFile(const std::filesystem::path &path)
{
	std::ifstream file{path, std::ios::binary};
	REQUIRE(file);
	std::ostringstream contents;
	contents << file.rdbuf();
	return contents.str();
}

constexpr const char *kProgram = R"(version: 1
parameters:
  - name: baseColor
    default: [1, 1, 1, 1]
  - name: emissive
    default: [0, 0, 0, 0]
textures:
  - name: baseColor
    fallback: White
  - name: normal
    fallback: FlatNormal
)";

constexpr const char *kMaterial = R"(version: 1
program: lit
parameters:
  - name: baseColor
    value: [0.5, 0.25, 1, 1]
textures:
  - slot: normal
    path: textures/plate.linear.png
animated:
  - name: emissive
    wave: Triangle
    period: 4
    from: [0, 0, 0, 0]
    to: [2, 4, 6, 8]
)";
} // namespace

TEST_CASE("Material: a material and a program round-trip through the YAML archive")
{
	const MaterialDefinition material = ReadMaterial(kMaterial, "test.material.yaml");
	CHECK(material.program == "lit");
	REQUIRE(material.parameters.size() == 1);
	CHECK(material.parameters[0].value == glm::vec4{0.5f, 0.25f, 1.0f, 1.0f});
	REQUIRE(material.animated.size() == 1);
	CHECK(material.animated[0].wave == Wave::Triangle);
	CHECK(material.animated[0].period == 4.0);
	// Left out, so the default member initialiser.
	CHECK(material.animated[0].phase == 0.0);

	const MaterialDefinition again = ReadMaterial(WriteMaterial(material), "again");
	CHECK(again.program == material.program);
	REQUIRE(again.textures.size() == 1);
	CHECK(again.textures[0].slot == "normal");
	CHECK(again.textures[0].path == "textures/plate.linear.png");
	CHECK(again.animated[0].to == material.animated[0].to);

	const ProgramDefinition program = ReadProgram(kProgram, "lit.program.yaml");
	const ProgramDefinition programAgain = ReadProgram(WriteProgram(program), "again");
	REQUIRE(programAgain.textures.size() == 2);
	CHECK(programAgain.textures[1].fallback == TextureFallback::FlatNormal);
	CHECK(programAgain.parameters[1].name == "emissive");
}

TEST_CASE("Material: reading refuses what the format alone would let through")
{
	CHECK_THROWS_AS(ReadMaterial("version: 1\nparameters: []\n", "unnamed"), FormatError);
	CHECK_THROWS_AS(
	    ReadMaterial("version: 1\nprogram: mesh\nparameters:\n  - name: a\n  - name: a\n", "repeated"),
	    FormatError);
	CHECK_THROWS_AS(
	    ReadMaterial("version: 1\nprogram: mesh\nanimated:\n  - name: a\n    period: 0\n", "still"),
	    FormatError);
	CHECK_THROWS_AS(
	    ReadMaterial("version: 1\nprogram: mesh\ntextures:\n  - slot: baseColor\n", "pathless"),
	    FormatError);
	// A misspelt key is an error, not a field quietly left at its default.
	CHECK_THROWS_AS(ReadMaterial("version: 1\nprogramme: mesh\n", "misspelt"), FormatError);

	std::string tooMany = "version: 1\ntextures:\n";
	for (std::size_t slot = 0; slot <= kMaterialTextureSlots; ++slot)
		tooMany += "  - name: t" + std::to_string(slot) + "\n";
	CHECK_THROWS_AS(ReadProgram(tooMany, "wide"), FormatError);
}

TEST_CASE("Material: resolving binds parameters and textures in the program's order")
{
	const ProgramDefinition program = ReadProgram(kProgram, "lit.program.yaml");

	const ResolvedMaterial defaults = ResolveDefaults(program);
	REQUIRE(defaults.parameters.size() == 2);
	CHECK(defaults.parameters[0] == glm::vec4{1.0f});
	CHECK(defaults.textures[0].empty());
	CHECK(defaults.fallbacks[1] == TextureFallback::FlatNormal);

	const ResolvedMaterial resolved =
	    ResolveMaterial(program, ReadMaterial(kMaterial, "test"), "test.material.yaml");
	CHECK(resolved.parameters[0] == glm::vec4{0.5f, 0.25f, 1.0f, 1.0f});
	CHECK(resolved.parameters[1] == glm::vec4{0.0f});
	CHECK(resolved.textures[0].empty());
	CHECK(resolved.textures[1] == "textures/plate.linear.png");
	REQUIRE(resolved.animations.size() == 1);
	CHECK(resolved.animations[0].parameter == 1);

	MaterialDefinition unknownParameter;
	unknownParameter.program = "lit";
	unknownParameter.parameters.push_back({"roughness", glm::vec4{0.5f}});
	CHECK_THROWS_AS(ResolveMaterial(program, unknownParameter, "rough"), FormatError);
	MaterialDefinition unknownSlot;
	unknownSlot.program = "lit";
	unknownSlot.textures.push_back({"occlusion", "textures/ao.png"});
	CHECK_THROWS_AS(ResolveMaterial(program, unknownSlot, "occluded"), FormatError);
}

TEST_CASE("Material: waves run from 0 to 1 over a period, and time-driven values follow them")
{
	CHECK(EvaluateWave(Wave::Sine, 0.0) == doctest::Approx(0.0));
	CHECK(EvaluateWave(Wave::Sine, 0.5) == doctest::Approx(1.0));
	CHECK(EvaluateWave(Wave::Sine, 0.25) == doctest::Approx(0.5));
	CHECK(EvaluateWave(Wave::Triangle, 0.25) == doctest::Approx(0.5));
	CHECK(EvaluateWave(Wave::Triangle, 0.5) == doctest::Approx(1.0));
	CHECK(EvaluateWave(Wave::Saw, 0.75) == doctest::Approx(0.75));
	CHECK(EvaluateWave(Wave::Square, 0.49) == 0.0f);
	CHECK(EvaluateWave(Wave::Square, 0.51) == 1.0f);
	// Whole cycles fall away, negative ones too.
	CHECK(EvaluateWave(Wave::Saw, 3.25) == doctest::Approx(0.25));
	CHECK(EvaluateWave(Wave::Saw, -0.75) == doctest::Approx(0.25));

	const ProgramDefinition program = ReadProgram(kProgram, "lit.program.yaml");
	const ResolvedMaterial resolved = ResolveMaterial(program, ReadMaterial(kMaterial, "test"), "test");
	std::vector<glm::vec4> parameters{glm::vec4{-1.0f}};
	// A quarter of the way through the triangle's period: halfway up.
	EvaluateParameters(resolved, 1.0, parameters);
	REQUIRE(parameters.size() == 3);
	CHECK(parameters[0] == glm::vec4{-1.0f});
	CHECK(parameters[1] == glm::vec4{0.5f, 0.25f, 1.0f, 1.0f});
	CHECK(parameters[2].y == doctest::Approx(2.0));

	// Eleven and a half days in, the wave is where it was a second in: the cycle is taken in
	// double precision, so float time's loss of the fraction never reaches it.
	std::vector<glm::vec4> later;
	EvaluateParameters(resolved, 1'000'000.0 + 1.0, later);
	CHECK(later[1].y == doctest::Approx(parameters[2].y).epsilon(1e-4));
}

TEST_CASE("Material: the shipped materials resolve against the programs they name")
{
	const std::filesystem::path assets{JETTISON_ASSET_SOURCE_DIR};
	std::size_t materials = 0;
	for (const auto &entry : std::filesystem::directory_iterator{assets / "materials"})
	{
		const std::string name = entry.path().filename().string();
		if (!name.ends_with(".material.yaml"))
			continue;
		CAPTURE(name);
		const MaterialDefinition material = ReadMaterial(ReadFile(entry.path()), name);
		const auto programPath = assets / "shaders" / (material.program + ".program.yaml");
		REQUIRE(std::filesystem::is_regular_file(programPath));
		const ProgramDefinition program = ReadProgram(ReadFile(programPath), material.program);
		const ResolvedMaterial resolved = ResolveMaterial(program, material, name);
		for (const std::string &texture : resolved.textures)
			if (!texture.empty())
				CHECK(std::filesystem::is_regular_file(assets / texture));
		++materials;
	}
	CHECK(materials >= 3);
}
