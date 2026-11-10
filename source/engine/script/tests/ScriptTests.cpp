#include <jettison/script/ScriptHost.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Script: no modules are loaded; loading one fails loudly until phase 11")
{
	Jettison::Script::ScriptHost host;
	CHECK(host.LoadedModules() == 0);
	CHECK_THROWS_AS(host.Load("mods/example.wasm", {}), Jettison::Core::NotImplementedError);
}
