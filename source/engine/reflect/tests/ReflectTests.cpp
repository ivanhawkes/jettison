#include <jettison/reflect/Reflect.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Reflect: the attribute bits are distinct, and lookup fails loudly until phase 3")
{
	using Jettison::Reflect::Attribute;
	CHECK((static_cast<unsigned>(Attribute::Saved) & static_cast<unsigned>(Attribute::Console)) == 0);
	CHECK_THROWS_AS(
	    Jettison::Reflect::Resolve("TransformComponent"), Jettison::Core::NotImplementedError);
}
