#include "vulkan/Model.h"

#include <jettison/animation/ModelFile.h>
#include <jettison/animation/RigLoader.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

using namespace Jettison;
using namespace Jettison::Render;

TEST_CASE("Model: a model file is read once for its rig and its geometry, and once again a change")
{
	const std::filesystem::path directory =
	    std::filesystem::temp_directory_path() / ("jettison-model-tests-" + std::to_string(getpid()));
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory / "models");
	for (const char *name : {"arm.glb", "crate.glb"})
		std::filesystem::copy_file(
		    std::filesystem::path{JETTISON_TEST_CONTENT_DIR} / "models" / name,
		    directory / "models" / name);

	{
		Vfs::FileSystem files;
		files.AddMount({"base", directory, false});
		Jobs::IoWorker worker;
		Resource::ResourceManager manager{
		    files, worker, {.hotReload = true, .pollInterval = {}, .settleLoads = true}};
		Animation::RegisterRigLoader(manager);

		// The loader the renderer registers, less its GPU finish and retire.
		manager.RegisterLoader(
		    Resource::DerivedLoader<Model, Animation::ModelFile>{
		        [](const Animation::ModelFile &file, std::string_view)
		        { return Model::FromGltf(file); },
		        {},
		        {}});

		// The arm has a skin and a clip; the crate has neither, and its rig is its one mesh node.
		for (const std::string path : {"models/arm.glb", "models/crate.glb"})
		{
			CAPTURE(path);
			const auto rig = manager.Load<Animation::Rig>(path);
			const auto model = manager.Load<Model>(path);

			// The parse's own slot, which both hold: its revision counts the file's reads.
			const auto parse = manager.Load<Animation::ModelFile>(path);
			manager.Update();
			REQUIRE(manager.TryGet(rig) != nullptr);
			REQUIRE(manager.TryGet(model) != nullptr);
			CHECK_FALSE(manager.TryGet(model)->m_primitives.empty());
			CHECK(manager.TryGet(rig)->HasSkin() == (path == "models/arm.glb"));
			CHECK(manager.RevisionOf(parse) == 1);

			// Not kept once the rig and the geometry have it.
			CHECK(manager.StatusOf(parse) == Resource::Status::Ready);
			CHECK(manager.TryGet(parse) == nullptr);

			// A change on the disk (J14's hot reload) makes both again from one more read.
			const auto file = directory / path;
			std::filesystem::last_write_time(
			    file, std::filesystem::last_write_time(file) + std::chrono::seconds{1});
			manager.Update();
			manager.Update();
			CHECK(manager.RevisionOf(parse) == 2);
			CHECK(manager.RevisionOf(rig) == 2);
			CHECK(manager.RevisionOf(model) == 2);
			CHECK(manager.TryGet(rig) != nullptr);
			CHECK(manager.TryGet(model) != nullptr);
			manager.Release(parse);
			manager.Release(model);
			manager.Release(rig);
		}

		// Geometry alone, as a model the scene has no rig for: one read too.
		const auto model = manager.Load<Model>("models/crate.glb");
		const auto parse = manager.Load<Animation::ModelFile>("models/crate.glb");
		manager.Update();
		CHECK(manager.TryGet(model) != nullptr);
		CHECK(manager.RevisionOf(parse) == 1);
		manager.Release(parse);
		manager.Release(model);
	}
	std::error_code error;
	std::filesystem::remove_all(directory, error);
}
