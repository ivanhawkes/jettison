jettison_add_module(serialise
	DEPENDS
		core
		reflect
		vfs
	HEADERS
		include/jettison/serialise/Archive.h
		include/jettison/serialise/Binary.h
		include/jettison/serialise/Migration.h
	SOURCES
		src/Archive.cpp
		src/BinaryArchive.cpp
		src/Shape.cpp
		src/Shape.h
		src/YamlArchive.cpp
	TESTS
		tests/MigrationTests.cpp
		tests/SerialiseTests.cpp
	# glm is public because its vectors are field types the archives write, so a caller's fields
	# and the tests use them.
	PUBLIC_LIBRARIES
		glm
	PRIVATE_LIBRARIES
		yaml-cpp::yaml-cpp
)

# A YAML migration's step edits a YAML::Node, so the tests that write one include yaml-cpp.
if(TARGET jettison_serialise_tests)
	target_link_libraries(jettison_serialise_tests PRIVATE yaml-cpp::yaml-cpp)
endif()
