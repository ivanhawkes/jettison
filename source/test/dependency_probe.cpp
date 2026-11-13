// Every third-party library the engine has, or plans to have, linked into one executable with the
// engine, each called at least once. A duplicate symbol, two copies of one library, a definition
// two libraries disagree about, or a dependency that does not survive the project's warnings fails
// here, in the gate, instead of in the phase that first links it.
// source/external/CMakeLists.txt says why each library is configured the way it is.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

// In use today.
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
#include <fastgltf/core.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <stb_image.h>
// The implementation is the engine's: render compiles it, and a second copy here would be a
// duplicate definition of every VMA function.
#include <vk_mem_alloc.h>
#include <yaml-cpp/yaml.h>

// Planned.
#include <FastNoiseLite.h>
#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/RegisterTypes.h>
#include <RmlUi/Core/Core.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <ktx.h>
#include <mikktspace.h>
#include <miniaudio.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <tracy/Tracy.hpp>
#include <wasm_export.h>
#include <zstd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string_view>
#include <type_traits>

#include <jettison/render/FrameData.h>
#include <jettison/render/Mesh.h>

#ifndef SPDLOG_USE_STD_FORMAT
#error "spdlog must format with std::format: the executable carries one formatting library"
#endif

TEST_CASE("GLM: the geometry ABI the shaders and buffers agree on")
{
	static_assert(sizeof(Jettison::Render::Vertex) == 108);
	static_assert(sizeof(Jettison::Render::FrameUBO) == 624);
	static_assert(sizeof(Jettison::Render::GpuLight) == 64);
	static_assert(sizeof(glm::vec3) == 12 && alignof(glm::vec3) == 4);

	// The zero-to-one depth range the viewport is set up for (GLM_FORCE_DEPTH_ZERO_TO_ONE).
	const glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.25f, 0.1f, 10.0f);
	CHECK(projection[2][2] == doctest::Approx(-1.010101f));
	CHECK(projection[3][2] == doctest::Approx(-0.101010f));
}

TEST_CASE("EnTT")
{
	entt::registry registry;
	const entt::entity entity = registry.create();
	registry.emplace<int>(entity, 7);
	CHECK(registry.all_of<int>(entity));
	CHECK(registry.get<int>(entity) == 7);
}

TEST_CASE("GLFW and Dear ImGui")
{
	int major = 0;
	int minor = 0;
	int revision = 0;
	glfwGetVersion(&major, &minor, &revision);
	CHECK(major == 3);
	CHECK(minor == 5);

	IMGUI_CHECKVERSION();
	ImGuiContext *context = ImGui::CreateContext();
	CHECK(context != nullptr);
	ImGui::DestroyContext(context);
}

TEST_CASE("stb_image: the one implementation, compiled in the render module")
{
	const std::array<stbi_uc, 4> notAnImage{0, 1, 2, 3};
	int width = 0;
	int height = 0;
	int channels = 0;
	CHECK(stbi_info_from_memory(notAnImage.data(), 4, &width, &height, &channels) == 0);
}

TEST_CASE("yaml-cpp")
{
	CHECK(YAML::Load("answer: 42")["answer"].as<int>() == 42);
}

TEST_CASE("fastgltf and its simdjson")
{
	constexpr std::string_view json = R"({"asset":{"version":"2.0"}})";
	auto data = fastgltf::GltfDataBuffer::FromBytes(
	    reinterpret_cast<const std::byte *>(json.data()), json.size());
	REQUIRE(data.error() == fastgltf::Error::None);
	fastgltf::Parser parser;
	auto asset = parser.loadGltfJson(data.get(), {});
	REQUIRE(asset.error() == fastgltf::Error::None);
}

TEST_CASE("spdlog formats through std::format")
{
	std::ostringstream stream;
	auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream);
	spdlog::logger logger("probe", sink);
	logger.set_pattern("%v");
	logger.info("{} + {} = {}", 2, 2, 4);
	logger.flush();
	CHECK(stream.str().starts_with("2 + 2 = 4"));
}

TEST_CASE("Tracy compiles to nothing, or to an on-demand client")
{
	ZoneScopedN("dependency probe");
	FrameMark;
	CHECK(true);
}

TEST_CASE("Vulkan Memory Allocator: the renderer's copy of the implementation links")
{
	decltype(&vmaCreateAllocator) volatile create = &vmaCreateAllocator;
	CHECK(create != nullptr);
}

TEST_CASE("FastNoiseLite is deterministic for a seed")
{
	FastNoiseLite first(1337);
	FastNoiseLite second(1337);
	first.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	second.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	const float value = first.GetNoise(1.5f, 2.5f);
	CHECK(value >= -1.0f);
	CHECK(value <= 1.0f);
	CHECK(value == second.GetNoise(1.5f, 2.5f));
}

namespace
{
// A unit quad in the XY plane, facing +Z, with UVs that follow X and Y: its tangent is +X.
struct TangentQuad
{
	std::array<glm::vec3, 4> positions{
	    glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 0.0f},
	    glm::vec3{0.0f, 1.0f, 0.0f}};
	std::array<glm::vec4, 4> tangents{};
};

TangentQuad &QuadOf(const SMikkTSpaceContext *context)
{
	return *static_cast<TangentQuad *>(context->m_pUserData);
}
} // namespace

TEST_CASE("MikkTSpace")
{
	SMikkTSpaceInterface callbacks{};
	callbacks.m_getNumFaces = [](const SMikkTSpaceContext *) { return 1; };
	callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext *, int) { return 4; };
	callbacks.m_getPosition = [](const SMikkTSpaceContext *context, float out[], int, int vertex)
	{
		const glm::vec3 &position = QuadOf(context).positions[static_cast<std::size_t>(vertex)];
		out[0] = position.x;
		out[1] = position.y;
		out[2] = position.z;
	};
	callbacks.m_getNormal = [](const SMikkTSpaceContext *, float out[], int, int)
	{
		out[0] = 0.0f;
		out[1] = 0.0f;
		out[2] = 1.0f;
	};
	callbacks.m_getTexCoord = [](const SMikkTSpaceContext *context, float out[], int, int vertex)
	{
		const glm::vec3 &position = QuadOf(context).positions[static_cast<std::size_t>(vertex)];
		out[0] = position.x;
		out[1] = position.y;
	};
	callbacks.m_setTSpaceBasic =
	    [](const SMikkTSpaceContext *context, const float tangent[], float sign, int, int vertex)
	{
		QuadOf(context).tangents[static_cast<std::size_t>(vertex)] = {
		    tangent[0], tangent[1], tangent[2], sign};
	};

	TangentQuad quad;
	SMikkTSpaceContext context{&callbacks, &quad};
	REQUIRE(genTangSpaceDefault(&context) != 0);
	for (const glm::vec4 &tangent : quad.tangents)
	{
		CHECK(tangent.x == doctest::Approx(1.0f));
		CHECK(tangent.w == doctest::Approx(1.0f));
	}
}

TEST_CASE("Jolt in double precision")
{
	static_assert(std::is_same_v<JPH::Real, double>, "simulation positions are doubles");

	// A position a million kilometres out keeps a quarter of a metre, which a float cannot.
	JPH::RVec3 position(1.0e9, 0.0, 0.0);
	position += JPH::RVec3(0.25, 0.0, 0.0);
	CHECK(position.GetX() - 1.0e9 == 0.25);

	JPH::RegisterDefaultAllocator();
	auto factory = std::make_unique<JPH::Factory>();
	JPH::Factory::sInstance = factory.get();
	JPH::RegisterTypes();
	{
		const JPH::SphereShapeSettings settings(1.0f);
		const JPH::ShapeSettings::ShapeResult result = settings.Create();
		REQUIRE(result.IsValid());
		CHECK(result.Get()->GetInnerRadius() == doctest::Approx(1.0f));
	}
	JPH::UnregisterTypes();
	JPH::Factory::sInstance = nullptr;
}

TEST_CASE("FreeType, the build RmlUi was configured against")
{
	FT_Library library = nullptr;
	REQUIRE(FT_Init_FreeType(&library) == 0);
	FT_Int major = 0;
	FT_Int minor = 0;
	FT_Int patch = 0;
	FT_Library_Version(library, &major, &minor, &patch);
	CHECK(major == 2);
	CHECK(minor == 15);
	FT_Done_FreeType(library);
}

TEST_CASE("RmlUi")
{
	CHECK(!Rml::GetVersion().empty());
}

TEST_CASE("miniaudio")
{
	const ma_waveform_config config =
	    ma_waveform_config_init(ma_format_f32, 1, 48000, ma_waveform_type_sine, 0.5, 440.0);
	ma_waveform waveform;
	REQUIRE(ma_waveform_init(&config, &waveform) == MA_SUCCESS);
	std::array<float, 64> frames{};
	ma_uint64 read = 0;
	CHECK(ma_waveform_read_pcm_frames(&waveform, frames.data(), frames.size(), &read) == MA_SUCCESS);
	CHECK(read == frames.size());
	ma_waveform_uninit(&waveform);
}

TEST_CASE("KTX-Software: ktx_read creates a texture")
{
	ktxTextureCreateInfo info{};
	info.vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
	info.baseWidth = 4;
	info.baseHeight = 4;
	info.baseDepth = 1;
	info.numDimensions = 2;
	info.numLevels = 1;
	info.numLayers = 1;
	info.numFaces = 1;
	info.isArray = KTX_FALSE;
	info.generateMipmaps = KTX_FALSE;

	ktxTexture2 *texture = nullptr;
	REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS);
	CHECK(ktxTexture_GetDataSize(ktxTexture(texture)) == 4 * 4 * 4);
	ktxTexture_Destroy(ktxTexture(texture));
}

// One zstd in the executable: the engine's, which ktx_read is built against in place of the 1.5.5
// it carries (source/external/CMakeLists.txt).
TEST_CASE("zstd: one copy, the engine's, compresses and decompresses")
{
	CHECK(ZSTD_versionNumber() == 10507);
	CHECK(ZSTD_versionNumber() == ZSTD_VERSION_NUMBER);
	const std::string_view text = "a kilolitre of beer in casks, a kilolitre of beer in casks";
	std::array<char, 128> frame{};
	const std::size_t size = ZSTD_compress(frame.data(), frame.size(), text.data(), text.size(), 19);
	REQUIRE_FALSE(ZSTD_isError(size));
	CHECK(size < text.size());
	std::array<char, 128> back{};
	CHECK(ZSTD_decompress(back.data(), back.size(), frame.data(), size) == text.size());
	CHECK(std::string_view{back.data(), text.size()} == text);
}

TEST_CASE("WAMR runs a module")
{
	// (module (func (export "add") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add))
	std::array<std::uint8_t, 41> module{
	    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,                    // magic, version
	    0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f,              // type: (i32 i32) -> i32
	    0x03, 0x02, 0x01, 0x00,                                            // function: type 0
	    0x07, 0x07, 0x01, 0x03, 'a',  'd',  'd',  0x00, 0x00,              // export "add"
	    0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b}; // code

	REQUIRE(wasm_runtime_init());
	std::array<char, 128> error{};
	wasm_module_t loaded = wasm_runtime_load(
	    module.data(), static_cast<std::uint32_t>(module.size()), error.data(),
	    static_cast<std::uint32_t>(error.size()));
	REQUIRE_MESSAGE(loaded != nullptr, error.data());
	wasm_module_inst_t instance = wasm_runtime_instantiate(
	    loaded, 8192, 8192, error.data(), static_cast<std::uint32_t>(error.size()));
	REQUIRE_MESSAGE(instance != nullptr, error.data());
	wasm_function_inst_t add = wasm_runtime_lookup_function(instance, "add");
	REQUIRE(add != nullptr);
	wasm_exec_env_t environment = wasm_runtime_create_exec_env(instance, 8192);
	REQUIRE(environment != nullptr);

	std::array<std::uint32_t, 2> arguments{2, 40};
	CHECK(wasm_runtime_call_wasm(environment, add, 2, arguments.data()));
	CHECK(arguments[0] == 42);

	wasm_runtime_destroy_exec_env(environment);
	wasm_runtime_deinstantiate(instance);
	wasm_runtime_unload(loaded);
	wasm_runtime_destroy();
}
