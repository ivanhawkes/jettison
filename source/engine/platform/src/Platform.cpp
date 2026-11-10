#include <jettison/platform/Cpu.h>
#include <jettison/platform/Paths.h>

#include <jettison/core/Assert.h>

namespace Jettison::Platform
{
Paths ResolvePaths(const std::filesystem::path & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::filesystem::path ExecutablePath()
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<std::string> CheckCpuBaseline()
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Platform
