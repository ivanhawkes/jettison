#include <jettison/serialise/Archive.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

#include <string>

using namespace Jettison::Serialise;

TEST_CASE("Serialise: a version mismatch names both versions")
{
	const FormatVersionError error{"scene", 3, 1};
	const std::string message = error.what();
	CHECK(message.find("scene") != std::string::npos);
	CHECK(message.find('3') != std::string::npos);
	CHECK(message.find('1') != std::string::npos);
}

TEST_CASE("Serialise: the archives fail loudly until phase 3")
{
	CHECK_THROWS_AS(YamlArchive::Write({}, {}), Jettison::Core::NotImplementedError);
	CHECK_THROWS_AS(BinaryArchive::Write({}, {}), Jettison::Core::NotImplementedError);
}
