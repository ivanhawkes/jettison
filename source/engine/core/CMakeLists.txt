jettison_add_module(core
	HEADERS
		include/jettison/core/Assert.h
		include/jettison/core/Config.h
		include/jettison/core/File.h
		include/jettison/core/Handle.h
		include/jettison/core/Hash.h
		include/jettison/core/Log.h
		include/jettison/core/StringId.h
		include/jettison/core/Version.h
	SOURCES
		src/Assert.cpp
		src/Config.cpp
		src/File.cpp
		src/Log.cpp
		src/StringId.cpp
		src/Version.cpp
	TESTS
		tests/AssertTests.cpp
		tests/ConfigTests.cpp
		tests/FileTests.cpp
		tests/HashTests.cpp
		tests/LogTests.cpp
	PRIVATE_LIBRARIES
		spdlog::spdlog
		yaml-cpp::yaml-cpp
)

# The version a save records as the one that wrote it (Version.h).
set_source_files_properties(src/Version.cpp PROPERTIES
	COMPILE_DEFINITIONS "JETTISON_ENGINE_VERSION=\"${PROJECT_VERSION}\"")
