// The core declarations phase 2 fills in. Each fails loudly until then; nothing here may
// silently succeed, because every one of them is a report or a lookup someone acts on.
#include <jettison/core/Config.h>
#include <jettison/core/Log.h>
#include <jettison/core/StringId.h>

#include <jettison/core/Assert.h>

namespace Jettison::Core
{
void Log(
    Severity /*unused*/, const Category & /*unused*/, std::string_view /*unused*/,
    const std::source_location & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void InitLogging(Severity /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void ShutdownLogging() noexcept
{
}

void Config::LoadLayer(const std::filesystem::path & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void Config::Set(std::string_view /*unused*/, std::string /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<std::string> Config::GetString(std::string_view /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<double> Config::GetNumber(std::string_view /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<bool> Config::GetBool(std::string_view /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
void Config::SaveUserLayer(const std::filesystem::path & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}

StringId::StringId(std::string_view /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::string_view StringId::Text() const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Core
