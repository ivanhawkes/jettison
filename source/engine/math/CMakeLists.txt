# DETERMINISTIC: Random and Noise are part of the save format, and the double-precision bounds
# tests are called from simulation code, so their results must not depend on whether the compiler
# fused a multiply and an add.
jettison_add_module(math
	DETERMINISTIC
	DEPENDS
		core
	HEADERS
		include/jettison/math/Bounds.h
		include/jettison/math/Intersect.h
		include/jettison/math/Spline.h
		include/jettison/math/Easing.h
		include/jettison/math/Random.h
		include/jettison/math/Noise.h
		include/jettison/math/Tangents.h
	SOURCES
		src/Easing.cpp
		src/Intersect.cpp
		src/Noise.cpp
		src/Random.cpp
		src/Spline.cpp
		src/Tangents.cpp
	TESTS
		tests/CurveTests.cpp
		tests/IntersectTests.cpp
		tests/NoiseTests.cpp
		tests/RandomTests.cpp
		tests/TangentTests.cpp
	PUBLIC_LIBRARIES
		glm
	PRIVATE_LIBRARIES
		FastNoiseLite
		mikktspace
)
