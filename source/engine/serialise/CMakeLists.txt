jettison_add_module(serialise
	DEPENDS
		core
		reflect
		vfs
	HEADERS
		include/jettison/serialise/Archive.h
		include/jettison/serialise/Binary.h
	SOURCES
		src/Archive.cpp
		src/BinaryArchive.cpp
		src/Shape.cpp
		src/Shape.h
		src/YamlArchive.cpp
	TESTS
		tests/SerialiseTests.cpp
	# glm is public because its vectors are field types the archives write, so a caller's fields
	# and the tests use them.
	PUBLIC_LIBRARIES
		glm
	PRIVATE_LIBRARIES
		yaml-cpp::yaml-cpp
)
