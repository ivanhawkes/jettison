# The space-trading sample (docs/sample-game.md): goods, markets, traders, the fleet's shipyard and
# the market screen, as a game composed into the shell (App::Game). Jetsam and the render test link
# it; no engine module does. Its suite, `sample-space`, holds what was the shell's trading, traders,
# fleet, reasons and scripts tests.
add_library(jettison_sample_space STATIC
	include/jettison/sample/SpaceGame.h
	src/Fleet.cpp
	src/Fleet.h
	src/MarketScreen.cpp
	src/MarketScreen.h
	src/Repair.cpp
	src/Repair.h
	src/SpaceGame.cpp
	src/Traders.cpp
	src/Traders.h
	src/Trading.cpp
	src/Trading.h
)
target_include_directories(jettison_sample_space
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(jettison_sample_space PUBLIC Jettison::App PRIVATE jettison_warnings)
set_property(TARGET jettison_sample_space PROPERTY FOLDER "Samples")

# The sample's content, which Jetsam and the render test mount over the engine's assets as "base"
# (jettison_use_content): its definitions, star systems, scenes, models, materials, textures, sounds
# and music, its clips and sequences, the market screen and the strings of its goods and trades,
# with its package description, mod.yaml. Its pack, assets.pack beside the staged tree, is built for
# the smoke test, which runs from it.
jettison_add_assets(jettison_sample_space_assets
	SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/assets"
	DIRECTORIES animations cinematics defs grades materials scene strings systems textures ui
	CONFIGURED audio models
	FILES mod.yaml
)
set_property(TARGET jettison_sample_space_assets PROPERTY FOLDER "Samples")
jettison_add_pack(jettison_sample_space_pack
	SOURCE "${CMAKE_CURRENT_BINARY_DIR}/assets"
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/assets.pack"
	DEPENDS jettison_sample_space_assets
)

if(JETTISON_BUILD_TESTS)
	add_executable(jettison_sample_space_tests
		tests/FleetTests.cpp
		tests/GameTests.cpp
		tests/ReasonsTests.cpp
		tests/ModsTests.cpp
		tests/SampleGalaxy.h
		tests/ScriptsTests.cpp
		tests/StationsTests.cpp
		tests/TradersTests.cpp
		tests/TradingTests.cpp
	)
	target_include_directories(jettison_sample_space_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
	target_link_libraries(jettison_sample_space_tests PRIVATE
		jettison_sample_space doctest::doctest_with_main jettison_warnings)
	# The engine's assets and the sample's content under them; the smoke mod as source/test stages
	# it, and the Rust bindings its script is built against.
	target_compile_definitions(jettison_sample_space_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets"
		JETTISON_SAMPLE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
		JETTISON_SMOKE_MOD_DIR="${PROJECT_BINARY_DIR}/source/test/mods/smoke"
		JETTISON_TEST_SCRIPT_DIR="${PROJECT_SOURCE_DIR}/source/test/scripts"
		JETTISON_APP_TEST_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}")
	set_property(TARGET jettison_sample_space_tests PROPERTY FOLDER "Test")
	add_test(NAME sample-space COMMAND jettison_sample_space_tests)
endif()
