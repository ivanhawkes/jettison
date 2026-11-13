#include <jettison/sim/Reason.h>
#include <jettison/sim/Snapshot.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;

namespace
{
enum class Reason : std::uint8_t
{
	NoPadFits,
	ShortOfStock,
	Unregistered
};
enum class Misnamed : std::uint8_t
{
	Gone
};
enum class Unknown : std::uint8_t
{
	Gone
};

void RegisterReasons()
{
	static const bool registered = []
	{
		Sim::RegisterTypes();
		Reflect::RegisterEnum<Reason>("ReasonTests.Reason")
		    .Value<Reason::NoPadFits>("no_pad_fits")
		    .Value<Reason::ShortOfStock>("short_of_stock");
		Reflect::RegisterEnum<Misnamed>("ReasonTests.Misnamed").Value<Misnamed::Gone>("gone");
		return true;
	}();
	(void)registered;
}
} // namespace

TEST_CASE("Reason: a module's enumerator is its key, under the module's name")
{
	RegisterReasons();
	CHECK(Sim::MakeReason(Reason::NoPadFits).key == "reason.reasontests.no_pad_fits");
	CHECK(
	    Sim::ReasonKeys(entt::resolve<Reason>()) ==
	    std::vector<std::string>{
	        "reason.reasontests.no_pad_fits", "reason.reasontests.short_of_stock"});

	// Not a module's reasons, or not one of its reasons: a defect in the step, said at once.
	CHECK_THROWS_AS(Sim::MakeReason(Reason::Unregistered), std::logic_error);
	CHECK_THROWS_AS(Sim::MakeReason(Misnamed::Gone), std::logic_error);
	CHECK_THROWS_AS(Sim::MakeReason(Unknown::Gone), std::logic_error);
}

TEST_CASE("Reason: the arguments keep their order and kinds, through an archive and to a developer")
{
	RegisterReasons();
	const Sim::Reason reason = Sim::MakeReason(
	    Reason::ShortOfStock,
	    {Sim::ReasonArgument::Entity(entt::entity{42}), Sim::ReasonArgument::Integer(-12),
	     Sim::ReasonArgument::Identifier("copper", "Shell.Good"),
	     Sim::ReasonArgument::Identifier("Corva")});
	CHECK(
	    Sim::Describe(reason) ==
	    "reason.reasontests.short_of_stock(entity 0x2a, -12, Shell.Good 'copper', 'Corva')");
	CHECK(Sim::Describe(Sim::MakeReason(Reason::NoPadFits)) == "reason.reasontests.no_pad_fits()");

	const auto bytes = Serialise::BinaryArchive::Write(reason, Serialise::FormatVersion{1});
	CHECK(Serialise::BinaryArchive::Read<Sim::Reason>(bytes, Serialise::FormatVersion{1}) == reason);
}
