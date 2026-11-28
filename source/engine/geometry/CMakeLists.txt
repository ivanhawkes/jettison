# Triangle meshes as the cook and the loaders see them: a glTF file read, cooked or not (GltfFile), and
# a model cooked into the engine's GLB, its meshes given levels of detail (Cook.h). CPU only, so the
# cook and the CPU-only SDK build it; the device never appears here
# (docs/dense-geometry-and-ray-tracing.md, "Where the code goes").
jettison_add_module(geometry
	DEPENDS
		math
	HEADERS
		include/jettison/geometry/Cook.h
		include/jettison/geometry/GltfFile.h
	SOURCES
		src/Cook.cpp
		src/Extras.h
		src/GltfFile.cpp
	TESTS
		tests/GeometryTests.cpp
	PUBLIC_LIBRARIES
		fastgltf
	PRIVATE_LIBRARIES
		fastgltf::simdjson
		meshoptimizer
)

# The suite cooks the test content's models (source/test/content).
if(TARGET jettison_geometry_tests)
	target_compile_definitions(jettison_geometry_tests PRIVATE
		JETTISON_TEST_CONTENT_DIR="${PROJECT_SOURCE_DIR}/source/test/content")
endif()
