#include "TestTypes.h"

#include <jettison/script/Bindings.h>

#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace Jettison;
using ScriptTest::Inner;
using ScriptTest::Mood;
using ScriptTest::Ping;
using ScriptTest::Pong;

namespace
{
std::uint32_t OffsetOf(const Script::TypeLayout &layout, std::string_view field)
{
	for (const Script::FieldLayout &laid : layout.fields)
		if (laid.name == field)
			return laid.offset;
	FAIL("no field ", field);
	return 0;
}

std::string ReadText(const std::filesystem::path &path)
{
	std::ifstream file{path, std::ios::binary};
	std::ostringstream text;
	text << file.rdbuf();
	return text.str();
}
} // namespace

TEST_CASE("Script bindings: a type is laid out as a C compiler for wasm32 lays out its struct")
{
	ScriptTest::RegisterTypes();
	Script::Bindings bindings;
	const Script::TypeLayout &ping = bindings.Layout(entt::resolve<Ping>());
	CHECK(ping.cName == "Test_Ping");
	CHECK(OffsetOf(ping, "ship") == 0);
	CHECK(OffsetOf(ping, "count") == 8);
	CHECK(OffsetOf(ping, "value") == 16);
	CHECK(OffsetOf(ping, "text") == 24);
	CHECK(OffsetOf(ping, "numbers") == 32);
	CHECK(OffsetOf(ping, "inner") == 40);
	CHECK(OffsetOf(ping, "where") == 48);
	CHECK(OffsetOf(ping, "flag") == 72);
	CHECK(ping.size == 80);
	CHECK(ping.align == 8);
	// An optional is not one a script's memory holds, and is said to be left out.
	REQUIRE(ping.omitted.size() == 1);
	CHECK(ping.omitted[0].starts_with("hidden"));

	const Script::TypeLayout &inner = bindings.Layout(entt::resolve<Inner>());
	CHECK(inner.size == 8);
	CHECK(OffsetOf(inner, "weight") == 4);
	CHECK_THROWS_AS(bindings.Layout(entt::resolve<int>()), Script::BindingError);
}

TEST_CASE("Script bindings: a value written into a script's memory reads back as it was")
{
	ScriptTest::RegisterTypes();
	Script::Bindings bindings;
	const Script::TypeLayout &layout = bindings.Layout(entt::resolve<Pong>());
	Pong pong{entt::entity{7}, -40, "copper", {{Mood::Cross, 3}, {Mood::Calm, -2}}};
	const std::uint32_t size = bindings.SizeOf(layout, &pong);
	CHECK(size >= layout.size + 6 + 2 * 8);

	std::vector<std::byte> memory(4096);
	constexpr std::uint32_t kAt = 1000;
	bindings.Write(layout, &pong, memory, kAt);
	// The struct holds pointers into the same memory, as a script sees them.
	std::uint32_t text = 0;
	std::memcpy(&text, memory.data() + kAt + 16, 4);
	CHECK(text >= kAt + layout.size);
	CHECK(std::string_view{reinterpret_cast<const char *>(memory.data() + text), 6} == "copper");

	Pong read;
	bindings.Read(layout, memory, kAt, &read);
	CHECK(read.ship == pong.ship);
	CHECK(read.total == -40);
	CHECK(read.text == "copper");
	CHECK(read.items == pong.items);

	SUBCASE("a pointer past the end of memory is refused, naming the field")
	{
		const std::uint32_t wild = 4093;
		std::memcpy(memory.data() + kAt + 16, &wild, 4);
		CHECK_THROWS_WITH_AS(
		    bindings.Read(layout, memory, kAt, &read), doctest::Contains("Test.Pong.text"),
		    Script::BindingError);
	}
	SUBCASE("an array whose count would wrap is refused before anything is added")
	{
		const std::uint32_t count = 0x40000000;
		std::memcpy(memory.data() + kAt + 28, &count, 4);
		CHECK_THROWS_WITH_AS(
		    bindings.Read(layout, memory, kAt, &read), doctest::Contains("Test.Pong.items"),
		    Script::BindingError);
	}
	SUBCASE("an enumerator not registered is refused")
	{
		std::uint32_t items = 0;
		std::memcpy(&items, memory.data() + kAt + 24, 4);
		memory[items] = std::byte{9};
		CHECK_THROWS_WITH_AS(
		    bindings.Read(layout, memory, kAt, &read), doctest::Contains("Test.Pong.items[0].mood"),
		    Script::BindingError);
	}
	SUBCASE("a struct past the end of memory is refused")
	{
		CHECK_THROWS_AS(bindings.Read(layout, memory, 4090, &read), Script::BindingError);
		CHECK_THROWS_AS(bindings.Write(layout, &pong, memory, 4090), Script::BindingError);
	}
}

TEST_CASE("Script bindings: a bool that is neither 0 nor 1 is refused")
{
	ScriptTest::RegisterTypes();
	Script::Bindings bindings;
	const Script::TypeLayout &layout = bindings.Layout(entt::resolve<Ping>());
	std::vector<std::byte> memory(256);
	memory[72] = std::byte{2};
	Ping read;
	CHECK_THROWS_WITH_AS(
	    bindings.Read(layout, memory, 0, &read), doctest::Contains("Test.Ping.flag"),
	    Script::BindingError);
}

TEST_CASE(
    "Script bindings: the C header and the Rust module the suite's scripts compile against are the "
    "ones its types make")
{
	ScriptTest::RegisterTypes();
	Script::Bindings bindings;
	const std::array types{entt::resolve<Ping>()};
	const std::array commands{entt::resolve<Pong>()};
	const std::string header = Script::Header(bindings, types, commands);
	CHECK(
	    header.find(
	        "JETTISON_IMPORT(\"send:Test.Pong\") void jettison_send_Test_Pong(const Test_Pong "
	        "*command);") != std::string::npos);
	CHECK(header.find("#define Test_Mood_Cross ((Test_Mood)1u)") != std::string::npos);
	CHECK(header.find("hidden (") != std::string::npos);

	const std::string module = Script::RustModule(bindings, types, commands);
	CHECK(
	    module.find(
	        "#[link_name = \"send:Test.Pong\"]\n    pub fn jettison_send_Test_Pong(command: *const "
	        "Test_Pong);") != std::string::npos);
	CHECK(module.find("pub const Test_Mood_Cross: Test_Mood = 1;") != std::string::npos);
	// A field named for a keyword is a raw identifier.
	CHECK(module.find("pub r#where: jettison_dvec3,") != std::string::npos);

	// Both are checked in beside the suite's scripts, which compile against them.
	for (const auto &[name, text] :
	     {std::pair{"jettison_script.h", header}, std::pair{"jettison_script.rs", module}})
	{
		CAPTURE(name);
		const std::filesystem::path checked =
		    std::filesystem::path{JETTISON_SCRIPT_TEST_SOURCES} / name;
		const std::filesystem::path written = std::filesystem::path{JETTISON_SCRIPT_TEST_OUTPUT} / name;
		if (ReadText(checked) != text)
		{
			std::ofstream{written, std::ios::binary} << text;
			FAIL_CHECK(
			    "tests/scripts/", name, " is not what the suite's types make; what they make is at ",
			    written.string());
		}
	}
}
