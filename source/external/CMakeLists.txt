include(FetchContent)
find_package(Vulkan REQUIRED)

# Every third-party dependency, pinned. Versions were last reviewed on 2026-11-10 against each
# project's newest release; where a pin is older than that, the comment on it says why. The
# whole set is linked into one executable by source/test/dependency_probe.cpp, which the gate
# builds and runs, so a duplicate symbol, an ABI-affecting define that disagrees between two
# libraries, or a dependency that drags in a second copy of another is a gate failure rather
# than a surprise in a later phase.
#
# Plain variables set here reach this directory and the dependencies added from it, never the
# project's own targets. Two matter for every dependency below: everything is static, because
# RmlUi declares BUILD_SHARED_LIBS as an option defaulting to ON and every later dependency
# would follow it; and no dependency builds its own tests, because KTX-Software includes CTest,
# which turns BUILD_TESTING on for whoever comes after it.
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

# A dependency that declares a CMake older than 3.13 gets the old option() behaviour, which
# deletes a plain variable of the same name instead of honouring it. Tracy 0.13.1 is one: without
# this, the settings in its block below were discarded and it configured with its own defaults.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# ---------------------------------------------------------------------------------------------
# In use today.
# ---------------------------------------------------------------------------------------------

# EnTT 4 requires C++20 and CMake 3.28. Its one break in this tree was registry::has<>, which
# is all_of<> now.
set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v4.0.0
  GIT_SHALLOW TRUE
  SYSTEM
)
FetchContent_MakeAvailable(entt)

# The geometry ABI the shaders and buffers agree on - sizeof(Vertex) 108, sizeof(FrameUBO) 160,
# and the zero-to-one depth range below - was measured when the tree moved from 0.9.9.8 to
# 1.0.1, and is asserted on every version since by the dependency probe, so a GLM update that
# moves a buffer layout fails the gate instead of the frame.
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.3
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(glm)
add_library(glm INTERFACE)
target_include_directories(glm SYSTEM INTERFACE "${glm_SOURCE_DIR}")

# GLM's configuration is a compile definition, on the target that carries the headers: these
# macros have to be set before the first GLM include, and a header can only guarantee that for
# files that include it first. Model.h used to carry them, which leaves every other
# translation unit one include order away from a different GLM. Measured: with
# GLM_FORCE_DEPTH_ZERO_TO_ONE glm::perspective(45, 1.25, 0.1, 10) has [2][2] -1.010101 and
# [3][2] -0.101010, the zero-to-one depth range this renderer's viewport is set up for; without
# it the same call gives -1.020202 and -0.202020, the -1..1 range, and the scene renders with
# depth mapped somewhere else entirely.
target_compile_definitions(glm INTERFACE
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  GLM_ENABLE_EXPERIMENTAL
)

# GLM_FORCE_RADIANS is gone: GLM 1.0 has no degree mode, and radians were already the default
# in 0.9.9.8. GLM_FORCE_DEFAULT_ALIGNED_GENTYPES is gone with it, for a different reason - GLM
# disables aligned gentypes on this compiler (GLM_LANG carries no CXXMS flag, which the feature
# requires), so it changed nothing here: sizeof(glm::vec3) is 12 and alignof 4 on both versions,
# and the vertex stride the shaders read has always been the compact 108 bytes.

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Window backends. The desktop build is Wayland-only. GLFW's X11 backend is off by default
# and enabled by the clang-headless preset (JETTISON_GLFW_X11=ON), which is the tree the
# headless smoke tests run under a virtual display (see scripts/smoke-test.sh).
option(JETTISON_GLFW_X11 "Build GLFW with X11 support for headless smoke tests" OFF)
# One backend per tree. The headless presets ask for X11, and building Wayland alongside it
# leaves libwayland loaded, where it leaks 192 bytes at exit that LeakSanitizer reports
# against code we do not own (measured with clang-asan on wayland 1.26.0).
if(JETTISON_GLFW_X11)
	set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
else()
	set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
endif()
set(GLFW_BUILD_X11 ${JETTISON_GLFW_X11} CACHE BOOL "" FORCE)
# 3.5.1, not 3.5.0: upstream skipped 3.5 after publishing an incorrect tag for it.
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.5.1
  GIT_SHALLOW TRUE
  SYSTEM
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG c1aaac65e896a45f81f47a4d68f9959e0f86c859 # v1.93.0
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_demo.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(imgui SYSTEM PUBLIC
  "${imgui_SOURCE_DIR}"
  "${imgui_SOURCE_DIR}/backends"
)
target_compile_definitions(imgui PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# stb has no releases; this is the head of master on 2026-11-10. The image implementation is
# compiled once, in source/engine/render/src/vulkan/MaterialCache.cpp: nothing else in the set
# compiles an stb implementation (KTX-Software carries a copy of the headers on a private include
# path).
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 4e9b368bfc071e97206ea5c6adba68dd0cbf8897
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(stb)
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(yaml-cpp)

# fastgltf downloads the simdjson and simdutf amalgamations it was released against (simdjson
# 5.1.0 for fastgltf 1.0.1) at configure time. Its own pin is the tested one, so it stays, one
# patch release behind simdjson's 5.1.1.
set(FASTGLTF_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  fastgltf
  GIT_REPOSITORY https://github.com/spnda/fastgltf.git
  GIT_TAG v1.0.1
  GIT_SHALLOW TRUE
  SYSTEM
)
FetchContent_MakeAvailable(fastgltf)

# Unit tests: every engine module's suite links doctest_with_main, and the dependency probe links
# doctest. Fetched in every tree, planned dependencies or not, because the headless trees run the
# unit tests too.
block()
	set(DOCTEST_WITH_TESTS OFF)
	set(DOCTEST_NO_INSTALL ON)
	FetchContent_Declare(
	  doctest
	  GIT_REPOSITORY https://github.com/doctest/doctest.git
	  GIT_TAG v2.5.3
	  GIT_SHALLOW TRUE
	  SYSTEM
	  EXCLUDE_FROM_ALL
	)
	FetchContent_MakeAvailable(doctest)
endblock()

# Logging, behind core's Log.h. std::format rather than the bundled fmt, so the executable carries
# one formatting library: SPDLOG_USE_STD_FORMAT is a public definition of the spdlog target. In
# every tree since phase 2, because core links it.
block()
	set(SPDLOG_USE_STD_FORMAT ON)
	set(SPDLOG_BUILD_EXAMPLE OFF)
	set(SPDLOG_BUILD_TESTS OFF)
	set(SPDLOG_INSTALL OFF)
	FetchContent_Declare(
	  spdlog
	  GIT_REPOSITORY https://github.com/gabime/spdlog.git
	  GIT_TAG v1.17.0
	  GIT_SHALLOW TRUE
	  SYSTEM
	  EXCLUDE_FROM_ALL
	)
	FetchContent_MakeAvailable(spdlog)
endblock()

# Noise, behind math's Noise.h. Header-only, so it compiles with math's flags, deterministic ones
# included. In every tree since phase 2, because math links it.
FetchContent_Declare(
  FastNoiseLite
  GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
  GIT_TAG v1.1.1
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(FastNoiseLite)
add_library(FastNoiseLite INTERFACE)
target_include_directories(FastNoiseLite SYSTEM INTERFACE "${fastnoiselite_SOURCE_DIR}/Cpp")

# Tangent generation, behind math's Tangents.h: the reference MikkTSpace, which is what glTF
# specifies. It has no releases and one commit. In every tree since phase 2, because math links it.
FetchContent_Declare(
  mikktspace
  GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
  GIT_TAG 3e895b49d05ea07e4c2133156cfa94369e19e409
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(mikktspace)
add_library(mikktspace STATIC EXCLUDE_FROM_ALL "${mikktspace_SOURCE_DIR}/mikktspace.c")
target_include_directories(mikktspace SYSTEM PUBLIC "${mikktspace_SOURCE_DIR}")

# Profiling, behind profile's Profile.h. Held at 0.13.1, two releases behind upstream's 0.15.0:
# Tracy's client and profiler must speak the same protocol version, which changes between minor
# releases, and the profiler the devenv nixpkgs pin provides is 0.13.1. Move this pin and the
# profiler together. On demand: a client compiled in costs nothing until a profiler connects, and
# listens on localhost only. In every tree since phase 2, because profile links it; with
# JETTISON_PROFILE off, TracyClient.cpp compiles to nothing and the macros expand to nothing.
# TRACY_ENABLE and the other settings are public definitions of the TracyClient target, so every
# translation unit that includes Tracy.hpp agrees on them.
option(JETTISON_PROFILE "Compile the Tracy profiler client in (TRACY_ENABLE)" OFF)
block()
	set(TRACY_ENABLE ${JETTISON_PROFILE})
	set(TRACY_ON_DEMAND ON)
	set(TRACY_ONLY_LOCALHOST ON)
	set(TRACY_STATIC ON)
	FetchContent_Declare(
	  tracy
	  GIT_REPOSITORY https://github.com/wolfpld/tracy.git
	  GIT_TAG v0.13.1
	  GIT_SHALLOW TRUE
	  SYSTEM
	  EXCLUDE_FROM_ALL
	)
	FetchContent_MakeAvailable(tracy)
endblock()

# GPU memory. Header-only, and built against the same Vulkan headers as the renderer; render
# compiles the implementation in one translation unit (src/vulkan/Memory.cpp), and its public
# headers forward-declare VMA's handles, so the dependency stays private.
FetchContent_Declare(
  VulkanMemoryAllocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG v3.5.0
  GIT_SHALLOW TRUE
  SYSTEM
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# ---------------------------------------------------------------------------------------------
# Planned: the libraries docs/roadmap.md names for later phases. They are fetched now so the
# whole set is proven to link together before a phase depends on any of it, and they are
# EXCLUDE_FROM_ALL, so a tree builds only what something links: today that is the dependency
# probe, and nothing in the engine.
#
# Each is configured inside a block(), because their option names are generic
# (DOUBLE_PRECISION, INTERPROCEDURAL_OPTIMIZATION, ENABLE_INSTALL) and a cache entry for one of
# them would configure whichever other dependency reads the same name. Plain variables in a
# block reach the dependency added inside it and nothing after it.
# ---------------------------------------------------------------------------------------------
option(JETTISON_PLANNED_DEPENDENCIES
	"Fetch the libraries later roadmap phases use, and build the probe that links them all" ON)

if(JETTISON_PLANNED_DEPENDENCIES)

# Physics (phase 7). Every setting here is one the engine has to agree with:
#  - Double precision, because simulation positions are doubles in the system frame
#    (docs/architecture.md). CROSS_PLATFORM_DETERMINISTIC, because the simulation must give the
#    same answer in every build of the engine: without it Jolt compiles with
#    -ffp-contract=fast, and whether a multiply and an add are fused then depends on the
#    optimisation level, so a replay recorded in a Debug build can diverge in Release. Under it
#    Jolt turns contraction off, as the coding standards do for the engine's simulation code.
#  - RTTI and exceptions on, as in the rest of the executable. A class the engine derives from a
#    Jolt base compiled without RTTI needs type information the library never emitted.
#  - The Vulkan and CPU compute backends off (they serve hair and cloth), and Jolt's own
#    warnings-as-errors, install, LTO, flag overrides and sample programs off, so it builds with
#    the toolchain's flags and nothing else.
# Jolt adds its instruction-set flags (-mavx2 -mbmi -mpopcnt -mlzcnt -mf16c) as PUBLIC compile
# options, so they reach every target that links it: the engine's CPU baseline
# becomes x86-64 with AVX2 when phase 7 links Jolt into the engine. That is the decided minimum
# (docs/roadmap.md, "Decided"); USE_AVX512 stays off, because raising it is a new decision.
block()
	set(DOUBLE_PRECISION ON)
	set(CROSS_PLATFORM_DETERMINISTIC ON)
	set(CPP_RTTI_ENABLED ON)
	set(CPP_EXCEPTIONS_ENABLED ON)
	set(JPH_USE_VK OFF)
	set(JPH_USE_CPU_COMPUTE OFF)
	set(ENABLE_ALL_WARNINGS OFF)
	set(ENABLE_INSTALL OFF)
	set(INTERPROCEDURAL_OPTIMIZATION OFF)
	set(OVERRIDE_CXX_FLAGS OFF)
	set(TARGET_UNIT_TESTS OFF)
	set(TARGET_HELLO_WORLD OFF)
	set(TARGET_PERFORMANCE_TEST OFF)
	set(TARGET_SAMPLES OFF)
	set(TARGET_VIEWER OFF)
	FetchContent_Declare(
	  JoltPhysics
	  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
	  GIT_TAG v5.6.0
	  GIT_SHALLOW TRUE
	  SOURCE_SUBDIR Build
	  SYSTEM
	  EXCLUDE_FROM_ALL
	)
	FetchContent_MakeAvailable(JoltPhysics)
endblock()

# Fonts (phase 8), for RmlUi. OVERRIDE_FIND_PACKAGE answers RmlUi's find_package(Freetype) with
# this build, so a system FreeType can never be picked up on one machine and not another. The
# optional codecs are off: the engine loads TrueType and OpenType fonts, and each codec is a
# system library that would be found on a developer's machine and missing on a runner.
block()
	set(FT_DISABLE_ZLIB ON)
	set(FT_DISABLE_BZIP2 ON)
	set(FT_DISABLE_PNG ON)
	set(FT_DISABLE_HARFBUZZ ON)
	set(FT_DISABLE_BROTLI ON)
	set(SKIP_INSTALL_ALL ON)
	FetchContent_Declare(
	  freetype
	  GIT_REPOSITORY https://github.com/freetype/freetype.git
	  GIT_TAG VER-2-15-0
	  GIT_SHALLOW TRUE
	  SYSTEM
	  EXCLUDE_FROM_ALL
	  OVERRIDE_FIND_PACKAGE
	)
	FetchContent_MakeAvailable(freetype)
endblock()
add_library(Freetype::Freetype ALIAS freetype)

# Game UI (phase 8).
block()
	set(RMLUI_SAMPLES OFF)
	set(RMLUI_FONT_ENGINE freetype)
	set(RMLUI_LUA_BINDINGS OFF)
	set(RMLUI_TRACY_PROFILING OFF)
	set(RMLUI_WARNINGS_AS_ERRORS OFF)
	FetchContent_Declare(
	  RmlUi
	  GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
	  GIT_TAG 6.3
	  GIT_SHALLOW TRUE
	  SYSTEM
	  EXCLUDE_FROM_ALL
	)
	FetchContent_MakeAvailable(RmlUi)
endblock()

# Audio (phase 8). The libvorbis and libopus decoders are off: each is a system library found on
# one machine and not another, and miniaudio decodes WAV, FLAC and MP3 itself. Its playback
# backends (PulseAudio, ALSA, JACK) are loaded at run time, not linked.
block()
	set(MINIAUDIO_NO_LIBVORBIS ON)
	set(MINIAUDIO_NO_LIBOPUS ON)
	set(MINIAUDIO_BUILD_EXAMPLES OFF)
	set(MINIAUDIO_BUILD_TESTS OFF)
	set(MINIAUDIO_BUILD_TOOLS OFF)
	FetchContent_Declare(
	  miniaudio
	  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
	  GIT_TAG 0.11.26
	  GIT_SHALLOW TRUE
	  SYSTEM
	  EXCLUDE_FROM_ALL
	)
	FetchContent_MakeAvailable(miniaudio)
endblock()

# Textures (phase 5). v4.4.2, not v5.0.0-rc2: v5 is a release candidate.
#  - The engine links ktx_read (transcode and upload), never ktx and ktx_read together: both
#    compile the same Basis Universal transcoder and the same zstd.
#  - libktx compiles its own zstd 1.5.5 (external/basisu/zstd/zstd.c, public ZSTD_* symbols),
#    in v4.4.2 and still in v5.0.0-rc2. That is why the standalone zstd the roadmap names for
#    pack files is not in this set: a second zstd in the same executable is duplicate symbols,
#    or two versions of one library resolving each other's calls. See docs/roadmap.md.
#  - ETC unpacking is off: it is Ericsson's code under its own licence, and desktop GPUs do not
#    need ETC. OpenGL upload, tools, tests and documentation are off. KTX 1 stays on, although
#    the engine reads KTX 2 only: with KTX_FEATURE_KTX1 off, ktx_read's texture.c still calls
#    ktxTexture1_constructFromStreamAndHeader, which is then not compiled, and the link fails.
#  - The clone is spelled out rather than left to GIT_SHALLOW, for two reasons. CMake's shallow
#    clone passes --no-single-branch, which fetches the tip of every one of KTX's 45 refs: a
#    234 MiB pack, against 85 MiB for the tag alone (measured 2026-11-10). And the repository
#    keeps prebuilt binaries in Git LFS, which a machine with the LFS filter installed globally
#    would download into the dependency tree; the clone switches the filter off, so those files
#    are pointers, and none of the library's sources are among them. It is a git clone, not a
#    release archive, because KTX's version script runs git in its source directory and stops
#    the configure when that fails.
#  - The ASTC encoder KTX bundles compiles with -Werror by default. Under the devenv toolchain a
#    Debug build fails on it: the Nix compiler wrapper defines _FORTIFY_SOURCE, and glibc 2.44
#    warns that it needs optimisation, which -O0 does not have. A dependency's warnings are not
#    this project's gate, so ASTCENC_WERROR is off.
#  - libktx publicly defines DEBUG in Debug builds. It is linked privately by whatever uses it,
#    so the definition stays out of public engine headers.
block()
	set(KTX_FEATURE_TESTS OFF)
	set(KTX_FEATURE_TOOLS OFF)
	set(KTX_FEATURE_DOC OFF)
	set(KTX_FEATURE_JNI OFF)
	set(KTX_FEATURE_PY OFF)
	set(KTX_FEATURE_GL_UPLOAD OFF)
	set(KTX_FEATURE_VK_UPLOAD ON)
	set(KTX_FEATURE_ETC_UNPACK OFF)
	set(KTX_FEATURE_LOADTEST_APPS OFF)
	set(BASISU_SUPPORT_OPENCL OFF)
	set(ASTCENC_WERROR OFF)
	find_package(Git REQUIRED)
	FetchContent_Declare(
	  KTX-Software
	  DOWNLOAD_COMMAND "${CMAKE_COMMAND}" -E rm -rf <SOURCE_DIR>
	  COMMAND "${GIT_EXECUTABLE}" -c advice.detachedHead=false clone --quiet
	    --depth 1 --single-branch --branch v4.4.2
	    --config filter.lfs.smudge= --config filter.lfs.process= --config filter.lfs.required=false
	    https://github.com/KhronosGroup/KTX-Software.git <SOURCE_DIR>
	  SYSTEM
	  EXCLUDE_FROM_ALL
	)
	FetchContent_MakeAvailable(KTX-Software)
endblock()

# Scripting (phase 11). WAMR is the candidate runtime; docs/roadmap.md leaves the choice open
# against wasmtime until the phase starts. WAMR's top-level CMakeLists.txt is the build for
# WAMR's own iwasm program, with AOT and WASI on by default. The library is built from WAMR's
# documented embedding script instead, in a directory of its own, because that script sets C
# flags and compile definitions for the whole directory it runs in.
FetchContent_Declare(
  wamr
  GIT_REPOSITORY https://github.com/bytecodealliance/wasm-micro-runtime.git
  GIT_TAG WAMR-2.4.5
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(wamr)
add_subdirectory(wamr)

endif()
