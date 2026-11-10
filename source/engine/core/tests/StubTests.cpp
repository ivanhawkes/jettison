#include <jettison/core/Config.h>
#include <jettison/core/Handle.h>
#include <jettison/core/Log.h>
#include <jettison/core/StringId.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

using namespace Jettison::Core;

TEST_CASE("Handle: null by default, typed by tag, compared by index and generation")
{
	struct TextureTag;
	using TextureHandle = Handle<TextureTag>;
	static_assert(TextureHandle{}.IsNull());
	static_assert(TextureHandle{3, 1} == TextureHandle{3, 1});
	static_assert(TextureHandle{3, 1} != TextureHandle{3, 2});
	CHECK_FALSE(TextureHandle{0, 1}.IsNull());
}

TEST_CASE("StringId: null by default; hashing fails loudly until phase 2 pins the function")
{
	constexpr StringId null;
	static_assert(null.IsNull());
	CHECK_THROWS_AS(StringId{"models/Fox.glb"}, NotImplementedError);
}

TEST_CASE("Log and Config: fail loudly until phase 2, and shutting logging down is always safe")
{
	CHECK_THROWS_AS(Log(Severity::Info, Category{"test"}, "hello"), NotImplementedError);
	CHECK_THROWS_AS(InitLogging(Severity::Info), NotImplementedError);
	ShutdownLogging();
	const Config config;
	CHECK_THROWS_AS(config.GetString("render.vsync"), NotImplementedError);
}
