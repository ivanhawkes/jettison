#include <jettison/core/Handle.h>
#include <jettison/core/Hash.h>
#include <jettison/core/StringId.h>

#include <doctest/doctest.h>

#include <string>

using namespace Jettison::Core;

// The published FNV-1a 64 test vectors, and two of the engine's own names. These values are in
// save files and data from the day IDs are first stored: if one of these checks fails, the hash
// has changed, and every stored ID with it.
TEST_CASE("Fnv1a64: the published reference vectors, at compile time and at run time")
{
	static_assert(Fnv1a64("") == 0xcbf29ce484222325ULL);
	static_assert(Fnv1a64("a") == 0xaf63dc4c8601ec8cULL);
	static_assert(Fnv1a64("foobar") == 0x85944171f73967e8ULL);
	static_assert(Fnv1a64("models/Fox.glb") == 0x687133191a2e266cULL);

	const std::string runtime{"render.vsync"};
	CHECK(Fnv1a64(runtime) == 0x5bb5878b1c1cabf2ULL);
}

TEST_CASE("HashCombine: recorded values, and the order of the parts matters")
{
	static_assert(HashCombine(0, 0) == 0x9e3779b97f4a7c15ULL);
	static_assert(HashCombine(0, 1) == 0x9e3779b97f4a7c16ULL);
	constexpr auto render = Fnv1a64("render");
	constexpr auto vsync = Fnv1a64("vsync");
	static_assert(HashCombine(render, vsync) == 0xa87ac99364465d8eULL);
	static_assert(HashCombine(vsync, render) == 0x057dd5291a5f9906ULL);
}

TEST_CASE("StringId: the same hash from a constant and from a run-time string")
{
	constexpr StringId fromLiteral{"models/Fox.glb"};
	static_assert(fromLiteral.Hash() == Fnv1a64("models/Fox.glb"));

	const std::string path = std::string{"models/"} + "Fox.glb";
	const StringId fromString{path};
	CHECK(fromString == fromLiteral);
	CHECK(fromString != StringId{"models/Duck.glb"});
}

TEST_CASE("StringId: null by default, and the empty string is not null")
{
	static_assert(StringId{}.IsNull());
	static_assert(!StringId{""}.IsNull());
}

TEST_CASE("StringId: development builds name the text an ID came from")
{
	const StringId id{std::string{"definitions/ships/hauler"}};
#ifdef NDEBUG
	CHECK(id.Text() == "<unknown>");
#else
	CHECK(id.Text() == "definitions/ships/hauler");
	// Made only at compile time: not in the table, until run-time code hashes the same text.
	constexpr StringId compileTimeOnly{"never/hashed/at/run/time"};
	CHECK(compileTimeOnly.Text() == "<unknown>");
#endif
	CHECK(StringId{}.Text() == "<unknown>");
}

TEST_CASE("Handle: null by default, typed by tag, compared by index and generation")
{
	struct TextureTag;
	using TextureHandle = Handle<TextureTag>;
	static_assert(TextureHandle{}.IsNull());
	static_assert(TextureHandle{3, 1} == TextureHandle{3, 1});
	static_assert(TextureHandle{3, 1} != TextureHandle{3, 2});
	CHECK_FALSE(TextureHandle{0, 1}.IsNull());
}
