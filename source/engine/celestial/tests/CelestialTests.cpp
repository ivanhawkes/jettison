#include <jettison/celestial/Frames.h>
#include <jettison/celestial/Orbit.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

using namespace Jettison::Celestial;

TEST_CASE("Celestial: positions are double precision and the stubs fail loudly until phase 4")
{
	static_assert(sizeof(SystemPosition::metres.x) == sizeof(double));
	const SystemPosition camera{{1.0e11, 0.0, 0.0}};
	CHECK_THROWS_AS(ToRenderSpace(camera, camera), Jettison::Core::NotImplementedError);
	CHECK_THROWS_AS(Evaluate(OrbitalElements{}, 0.0), Jettison::Core::NotImplementedError);
}
