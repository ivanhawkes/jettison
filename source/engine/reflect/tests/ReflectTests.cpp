#include <jettison/reflect/Reflect.h>

#include <doctest/doctest.h>

#include <string>
#include <string_view>

// The reflection tables are global, and doctest runs every case in one process, so each case
// registers types of its own under names no other case uses.

using Jettison::Reflect::Attribute;

namespace
{
struct Cargo
{
	std::string good;
	int quantity{0};
	double mass{0.0};
	int reservedBy{0};
};

enum class DockState
{
	Free,
	Approaching,
	Docked,
	Departing,
};

struct Unregistered
{
	int value{0};
};

// Names whose 32-bit FNV-1a hashes collide, checked against an independent implementation:
// "costarring" and "liquid" both hash to 0x5e4daa9d, "declinate" and "macallums" to 0xe20e47d2.
struct CollidingFirst
{
};
struct CollidingSecond
{
};
struct CollidingFields
{
	int declinate{0};
	int macallums{0};
};
} // namespace

TEST_CASE("Reflect: attributes combine as a bit set, and None is in every set")
{
	using Jettison::Reflect::HasAll;
	const Attribute set = Attribute::Saved | Attribute::Console;
	CHECK(HasAll(set, Attribute::Saved));
	CHECK(HasAll(set, Attribute::Saved | Attribute::Console));
	CHECK_FALSE(HasAll(set, Attribute::Saved | Attribute::Editable));
	CHECK(HasAll(Attribute::None, Attribute::None));
	CHECK_FALSE(HasAll(Attribute::None, Attribute::Moddable));
}

TEST_CASE("Reflect: a name's ID is EnTT's hash of its text")
{
	constexpr Jettison::Reflect::Name name{"Cargo"};
	static_assert(name.Id() == entt::hashed_string::value("Cargo"));
	CHECK(std::string_view{name.Text()} == "Cargo");
}

TEST_CASE("Reflect: a registered type resolves by name, and lists its fields in registration order")
{
	using namespace Jettison::Reflect;
	RegisterType<Cargo>("Cargo", Attribute::Saved)
	    .Field<&Cargo::good>("good", Attribute::Saved | Attribute::Editable)
	    .Field<&Cargo::quantity>("quantity", Attribute::Saved)
	    .Field<&Cargo::mass>("mass", Attribute::Saved | Attribute::Console)
	    .Field<&Cargo::reservedBy>("reservedBy");

	const entt::meta_type type = Resolve("Cargo");
	REQUIRE(type);
	CHECK(type == entt::resolve<Cargo>());
	CHECK(type.name() == "Cargo");
	CHECK(AttributesOf(type) == Attribute::Saved);

	const auto all = Fields(type);
	REQUIRE(all.size() == 4);
	CHECK(all[0].name() == "good");
	CHECK(all[1].name() == "quantity");
	CHECK(all[2].name() == "mass");
	CHECK(all[3].name() == "reservedBy");

	// The archive writes the Saved fields; runtime state such as a reservation stays out.
	const auto saved = Fields(type, Attribute::Saved);
	REQUIRE(saved.size() == 3);
	CHECK(saved[2].name() == "mass");
	const auto console = Fields(type, Attribute::Console);
	REQUIRE(console.size() == 1);
	CHECK(console[0].name() == "mass");
	CHECK(AttributesOf(all[0]) == (Attribute::Saved | Attribute::Editable));
	CHECK(AttributesOf(all[3]) == Attribute::None);

	CHECK(FindField(type, "quantity") == all[1]);
	CHECK_FALSE(FindField(type, "volume"));
	CHECK(FindField(type, "quantity").type() == entt::resolve<int>());
}

TEST_CASE("Reflect: fields are read and written through the tables, as an archive does")
{
	using namespace Jettison::Reflect;
	struct Hold
	{
		int capacity{0};
		float fill{0.0f};
	};
	RegisterType<Hold>("ReflectTests.Hold")
	    .Field<&Hold::capacity>("capacity")
	    .Field<&Hold::fill>("fill");

	const entt::meta_type type = Resolve("ReflectTests.Hold");
	entt::meta_any hold = type.construct();
	REQUIRE(hold);
	CHECK(FindField(type, "capacity").set(hold, 400));
	CHECK(FindField(type, "fill").set(hold, 0.25f));

	const Hold &typed = hold.cast<const Hold &>();
	CHECK(typed.capacity == 400);
	CHECK(typed.fill == 0.25f);
	CHECK(FindField(type, "capacity").get(hold).cast<int>() == 400);
}

TEST_CASE("Reflect: a field's address is found in the object, so a reader can walk it in place")
{
	using namespace Jettison::Reflect;
	struct Berth
	{
		int number{0};
		double length{0.0};
	};
	RegisterType<Berth>("ReflectTests.Berth")
	    .Field<&Berth::number>("number")
	    .Field<&Berth::length>("length");

	Berth berth{7, 120.5};
	const entt::meta_type type = Resolve("ReflectTests.Berth");
	CHECK(AddressOf(FindField(type, "number"), &berth) == &berth.number);
	CHECK(AddressOf(FindField(type, "length"), &berth) == &berth.length);
	*static_cast<int *>(AddressOf(FindField(type, "number"), &berth)) = 9;
	CHECK(berth.number == 9);
	CHECK(AddressOf(FindField(type, "number"), nullptr) == nullptr);
	CHECK(AddressOf(entt::meta_data{}, &berth) == nullptr);
}

TEST_CASE("Reflect: a name that is not registered resolves to nothing")
{
	using Jettison::Reflect::Resolve;
	CHECK_FALSE(Resolve("NoSuchType"));
	CHECK_FALSE(Resolve(""));
	// EnTT knows every type it has seen, registered or not; Reflect does not count those.
	CHECK_FALSE(Jettison::Reflect::Fields(entt::resolve<Unregistered>()).size());
}

TEST_CASE("Reflect: registering a type twice, or a name twice, throws naming it")
{
	using namespace Jettison::Reflect;
	struct Ship
	{
		int hull{0};
	};
	struct OtherShip
	{
	};
	struct Twice
	{
		int a{0};
		int b{0};
	};

	RegisterType<Ship>("ReflectTests.Ship").Field<&Ship::hull>("hull");
	CHECK_THROWS_WITH_AS(
	    RegisterType<Ship>("ReflectTests.Ship2"),
	    doctest::Contains("already registered as 'ReflectTests.Ship'"), RegistrationError);
	CHECK_THROWS_WITH_AS(
	    RegisterType<OtherShip>("ReflectTests.Ship"),
	    doctest::Contains("name 'ReflectTests.Ship' is already registered"), RegistrationError);

	auto builder = RegisterType<Twice>("ReflectTests.Twice");
	builder.Field<&Twice::a>("a");
	CHECK_THROWS_WITH_AS(
	    builder.Field<&Twice::b>("a"), doctest::Contains("already has a field 'a'"), RegistrationError);
	// The rejected field did not replace the first one.
	const Twice twice{.a = 1, .b = 2};
	CHECK(FindField(Resolve("ReflectTests.Twice"), "a").get(twice).cast<int>() == 1);
}

TEST_CASE("Reflect: names whose hashes collide are refused, and do not resolve to each other")
{
	using namespace Jettison::Reflect;
	static_assert(entt::hashed_string::value("costarring") == entt::hashed_string::value("liquid"));
	static_assert(entt::hashed_string::value("declinate") == entt::hashed_string::value("macallums"));

	RegisterType<CollidingFirst>("costarring");
	CHECK_THROWS_WITH_AS(
	    RegisterType<CollidingSecond>("liquid"), doctest::Contains("hashes to the ID of 'costarring'"),
	    RegistrationError);
	CHECK(Resolve("costarring"));
	CHECK_FALSE(Resolve("liquid"));

	auto builder = RegisterType<CollidingFields>("ReflectTests.CollidingFields");
	builder.Field<&CollidingFields::declinate>("declinate");
	CHECK_THROWS_WITH_AS(
	    builder.Field<&CollidingFields::macallums>("macallums"),
	    doctest::Contains("hashes to the ID of 'declinate'"), RegistrationError);
	CHECK_FALSE(FindField(Resolve("ReflectTests.CollidingFields"), "macallums"));
}

TEST_CASE("Reflect: an enumeration converts between its values and their names")
{
	using namespace Jettison::Reflect;
	RegisterEnum<DockState>("DockState")
	    .Value<DockState::Free>("Free")
	    .Value<DockState::Approaching>("Approaching")
	    .Value<DockState::Docked>("Docked");

	const entt::meta_type type = Resolve("DockState");
	REQUIRE(type);
	CHECK(type.is_enum());

	CHECK(EnumName(entt::meta_any{DockState::Docked}) == "Docked");
	CHECK(EnumName(entt::meta_any{DockState::Free}) == "Free");
	// Departing was not registered, so it has no name to write.
	CHECK(EnumName(entt::meta_any{DockState::Departing}).empty());
	CHECK(EnumName(entt::meta_any{42}).empty());
	CHECK(EnumName(entt::meta_any{}).empty());

	const entt::meta_any docked = EnumValue(type, "Docked");
	REQUIRE(docked);
	CHECK(docked.cast<DockState>() == DockState::Docked);
	CHECK_FALSE(EnumValue(type, "Departing"));
	CHECK_FALSE(EnumValue(entt::resolve<Unregistered>(), "value"));

	CHECK_THROWS_AS(RegisterEnum<DockState>("DockState2"), RegistrationError);
}
