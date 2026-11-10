#include <jettison/script/ScriptHost.h>

#include <jettison/core/Assert.h>

namespace Jettison::Script
{
ModuleHandle ScriptHost::Load(const std::string & /*unused*/, const Limits & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void ScriptHost::Unload(ModuleHandle /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<std::vector<std::int32_t>> ScriptHost::Call(
    ModuleHandle /*unused*/, const std::string & /*unused*/,
    const std::vector<std::int32_t> & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Script
