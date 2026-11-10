#include <jettison/resource/ResourceHandle.h>
#include <jettison/resource/ResourceManager.h>

#include <jettison/core/Assert.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

TEST_CASE("Resource: a default handle is null and handles compare by index and generation")
{
	struct Texture
	{
	};
	using Handle = Jettison::Resource::ResourceHandle<Texture>;
	CHECK(Handle{}.IsNull());
	CHECK(Handle{3, 1} == Handle{3, 1});
	CHECK(Handle{3, 1} != Handle{3, 2});
}

TEST_CASE("Resource: the manager fails loudly until phase 3")
{
	Jettison::Vfs::FileSystem fileSystem;
	Jettison::Jobs::IoWorker worker;
	Jettison::Resource::ResourceManager manager{fileSystem, worker};
	CHECK_THROWS_AS(manager.Update(), Jettison::Core::NotImplementedError);
}
