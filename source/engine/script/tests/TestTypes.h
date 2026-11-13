#pragma once

#include <jettison/reflect/Reflect.h>

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The suite's own event and command, with a field of every kind a script's memory holds and one it
// does not; tests/scripts/jettison_script.h is the header they generate.
namespace ScriptTest
{
enum class Mood : std::uint8_t
{
	Calm,
	Cross
};

struct Inner
{
	Mood mood{Mood::Calm};
	std::int32_t weight{0};

	bool operator==(const Inner &) const = default;
};

struct Ping
{
	entt::entity ship{entt::null};
	std::int64_t count{0};
	double value{0.0};
	std::string text;
	std::vector<std::int32_t> numbers;
	Inner inner;
	glm::dvec3 where{0.0};
	std::optional<std::int32_t> hidden;
	bool flag{false};
};

struct Pong
{
	entt::entity ship{entt::null};
	std::int64_t total{0};
	std::string text;
	std::vector<Inner> items;
};

inline void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Jettison::Reflect;
		RegisterEnum<Mood>("Test.Mood").Value<Mood::Calm>("Calm").Value<Mood::Cross>("Cross");
		RegisterType<Inner>("Test.Inner").Field<&Inner::mood>("mood").Field<&Inner::weight>("weight");
		RegisterType<Ping>("Test.Ping", Attribute::Script)
		    .Field<&Ping::ship>("ship")
		    .Field<&Ping::count>("count")
		    .Field<&Ping::value>("value")
		    .Field<&Ping::text>("text")
		    .Field<&Ping::numbers>("numbers")
		    .Field<&Ping::inner>("inner")
		    .Field<&Ping::where>("where")
		    .Field<&Ping::hidden>("hidden")
		    .Field<&Ping::flag>("flag");
		RegisterType<Pong>("Test.Pong", Attribute::Script)
		    .Field<&Pong::ship>("ship", Attribute::Saved)
		    .Field<&Pong::total>("total", Attribute::Saved)
		    .Field<&Pong::text>("text", Attribute::Saved)
		    .Field<&Pong::items>("items", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace ScriptTest
