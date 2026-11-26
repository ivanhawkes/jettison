#pragma once

#include <jettison/reflect/Reflect.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/Snapshot.h>

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

// A sender nested in a command, which the host stamps as it does a top-level one.
struct Order
{
	Jettison::Sim::Controller by;
	std::int32_t amount{0};

	bool operator==(const Order &) const = default;
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

	// Whatever the script writes, the host's sender, here and in each order.
	Jettison::Sim::Controller sender;
	Order order;
	std::vector<Order> orders;
};

inline void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Jettison::Reflect;
		Jettison::Sim::RegisterTypes();
		RegisterEnum<Mood>("Test.Mood").Value<Mood::Calm>("Calm").Value<Mood::Cross>("Cross");
		RegisterType<Inner>("Test.Inner").Field<&Inner::mood>("mood").Field<&Inner::weight>("weight");
		RegisterType<Order>("Test.Order").Field<&Order::by>("by").Field<&Order::amount>("amount");
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
		    .Field<&Pong::items>("items", Attribute::Saved)
		    .Field<&Pong::sender>("sender", Attribute::Saved)
		    .Field<&Pong::order>("order", Attribute::Saved)
		    .Field<&Pong::orders>("orders", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace ScriptTest
