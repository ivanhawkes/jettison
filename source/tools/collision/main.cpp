#include "Bake.h"

#include <jettison/core/Log.h>
#include <jettison/physics/CollisionFile.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

// jettison_bake_collision <model> <collision file>: a glTF or GLB model's collision shape
// (Bake::CollisionShape) into the file physics reads (Physics::ReadCollisionFile), directories made
// as needed. The cook (source/tools/cook) does the same for every model it cooks.

namespace
{
constexpr Jettison::Core::Category kBake{"bake"};
} // namespace


int main(int argc, char **argv)
{
	using Jettison::Core::Log;
	using Jettison::Core::Severity;

	if (argc != 3)
	{
		Log(Severity::Error, kBake, "Usage: jettison_bake_collision <model> <collision file>");
		return 2;
	}
	const std::filesystem::path model{argv[1]};
	const std::filesystem::path output{argv[2]};
	try
	{
		const Jettison::Physics::ShapeDefinition shape = Jettison::Bake::CollisionShape(model);
		const std::vector<std::byte> bytes =
		    Jettison::Physics::WriteCollisionFile(shape, output.string());
		if (output.has_parent_path())
			std::filesystem::create_directories(output.parent_path());
		std::ofstream file{output, std::ios::binary};
		file.write(
		    reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!file.flush())
		{
			Log(Severity::Error, kBake, "{} could not be written", output.string());
			return 1;
		}
		Log(Severity::Info, kBake, "{}: {} parts from {}, {} bytes", output.string(),
		    shape.parts.size(), model.string(), bytes.size());
	}
	catch (const std::exception &error)
	{
		Log(Severity::Error, kBake, "{}", error.what());
		return 1;
	}
	return 0;
}
