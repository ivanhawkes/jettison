#include <jettison/vfs/FileSystem.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Vfs: mounts are kept in the order they are added; lookup fails loudly until phase 3")
{
	Jettison::Vfs::FileSystem fileSystem;
	fileSystem.AddMount({"base", "assets", false});
	fileSystem.AddMount({"user", "user", true});
	REQUIRE(fileSystem.Mounts().size() == 2);
	CHECK(fileSystem.Mounts()[0].name == "base");
	CHECK(fileSystem.Mounts()[1].writable);
	CHECK_THROWS_AS(fileSystem.Exists("models/Fox.glb"), Jettison::Core::NotImplementedError);
}
