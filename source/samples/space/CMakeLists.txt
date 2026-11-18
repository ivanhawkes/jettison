# The space-trading sample (docs/sample-game.md): goods, markets, traders, the fleet's shipyard and
# the market screen, as a game composed into the shell (App::Game). Jetsam and the render test link
# it; no engine module does. Its suite, `space`, holds what was the shell's trading, traders, fleet,
# reasons and scripts tests.
add_library(jettison_sample_space STATIC
	include/jettison/sample/SpaceGame.h
	src/Fleet.cpp
	src/Fleet.h
	src/MarketScreen.cpp
	src/MarketScreen.h
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

if(JETTISON_BUILD_TESTS)
	add_executable(jettison_sample_space_tests
		tests/FleetTests.cpp
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
	# The sample's star systems and definitions; the smoke mod as source/test stages it, and the
	# Rust bindings its script is built against.
	target_compile_definitions(jettison_sample_space_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets"
		JETTISON_SMOKE_MOD_DIR="${PROJECT_BINARY_DIR}/source/test/mods/smoke"
		JETTISON_TEST_SCRIPT_DIR="${PROJECT_SOURCE_DIR}/source/test/scripts"
		JETTISON_APP_TEST_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}")
	set_property(TARGET jettison_sample_space_tests PROPERTY FOLDER "Test")
	add_test(NAME space COMMAND jettison_sample_space_tests)
endif()
