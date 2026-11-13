#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Migration.h>

#include <doctest/doctest.h>

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison::Serialise;

// Named, not anonymous: EnTT identifies a type by its name, and the migration tests have a Cargo
// of their own in an anonymous namespace.
namespace LayerTests
{
// A market, as the sample game's definitions have one: records in a list, matched by a name.
struct Stock
{
	std::string good;
	std::uint32_t stock{0};
	std::uint32_t target{0};
};
struct Fees
{
	std::uint32_t docking{10};
	std::uint32_t trade{20};
};
struct Market
{
	std::string station;
	std::uint32_t feeBps{100};
	Fees fees;
	std::vector<Stock> goods;
	std::vector<std::string> tags;
	std::optional<std::string> note;
};
// A format with a rename in its history: version 1 called the tonnes "mass".
struct Cargo
{
	std::string label;
	std::uint32_t tonnes{0};
};

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Jettison::Reflect;
		RegisterType<Stock>("LayerTests.Stock")
		    .Field<&Stock::good>("good", Attribute::Saved)
		    .Field<&Stock::stock>("stock", Attribute::Saved)
		    .Field<&Stock::target>("target", Attribute::Saved);
		RegisterType<Fees>("LayerTests.Fees")
		    .Field<&Fees::docking>("docking", Attribute::Saved)
		    .Field<&Fees::trade>("trade", Attribute::Saved);
		RegisterType<Market>("LayerTests.Market")
		    .Field<&Market::station>("station", Attribute::Saved)
		    .Field<&Market::feeBps>("feeBps", Attribute::Saved)
		    .Field<&Market::fees>("fees", Attribute::Saved)
		    .Field<&Market::goods>("goods", Attribute::Saved)
		    .Field<&Market::tags>("tags", Attribute::Saved)
		    .Field<&Market::note>("note", Attribute::Saved);
		RegisterType<Cargo>("LayerTests.Cargo")
		    .Field<&Cargo::label>("label", Attribute::Saved)
		    .Field<&Cargo::tonnes>("tonnes", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace LayerTests

using namespace LayerTests;

namespace
{
constexpr std::string_view kBase = R"(version: 1
station: Ostra Highport
feeBps: 150
fees: {docking: 15, trade: 25}
goods:
  - {good: wheat-seeds, stock: 90, target: 60}
  - {good: beer, stock: 55, target: 40}
tags: [farming, hub]
note: the base's
)";

Market Layered(std::initializer_list<std::string_view> patches)
{
	RegisterTypes();
	std::vector<YamlLayer> layers{{"base", kBase}};
	int index = 0;
	for (const std::string_view patch : patches)
	{
		static const char *const kNames[] = {"first", "second", "third"};
		layers.push_back({kNames[index++], patch});
	}
	return YamlArchive::ReadLayered<Market>(layers, {1});
}

std::string Refusal(std::string_view patch)
{
	try
	{
		(void)Layered({patch});
	}
	catch (const FormatError &error)
	{
		return error.what();
	}
	FAIL("the patch was read: " << patch);
	return {};
}
} // namespace

TEST_CASE("Layers: one layer reads as the document does")
{
	RegisterTypes();
	const std::vector<YamlLayer> layers{{"base", kBase}};
	const auto market = YamlArchive::ReadLayered<Market>(layers, {1});
	CHECK(market.station == "Ostra Highport");
	CHECK(market.feeBps == 150);
	CHECK(market.goods.size() == 2);
	CHECK(market.note == "the base's");
	CHECK_THROWS_AS((void)YamlArchive::ReadLayered<Market>({}, {1}), std::invalid_argument);
}

TEST_CASE("Layers: a patch changes the fields it names and keeps the rest")
{
	const Market market = Layered({"version: 1\nfeeBps: 200\nfees: {trade: 30}\n"});
	CHECK(market.station == "Ostra Highport");
	CHECK(market.feeBps == 200);
	// A mapping is merged key by key.
	CHECK(market.fees.docking == 15);
	CHECK(market.fees.trade == 30);
	CHECK(market.goods.size() == 2);
	CHECK(market.tags == std::vector<std::string>{"farming", "hub"});
}

TEST_CASE("Layers: a null removes the field, which reads as its default")
{
	const Market market = Layered({"version: 1\nfeeBps: ~\nnote:\nfees: {docking: null}\n"});
	CHECK(market.feeBps == 100);
	CHECK_FALSE(market.note.has_value());
	CHECK(market.fees.docking == 10);
	CHECK(market.fees.trade == 25);
}

TEST_CASE("Layers: a sequence replaces the one below, or adds to it when tagged !append")
{
	CHECK(Layered({"version: 1\ntags: [refinery]\n"}).tags == std::vector<std::string>{"refinery"});
	CHECK(
	    Layered({"version: 1\ntags: !append [refinery]\n"}).tags ==
	    std::vector<std::string>{"farming", "hub", "refinery"});
	const Market block =
	    Layered({"version: 1\ngoods: !append\n  - {good: wine, stock: 5, target: 10}\n"});
	REQUIRE(block.goods.size() == 3);
	CHECK(block.goods[2].good == "wine");
	CHECK(block.goods[0].good == "wheat-seeds");
	// Over nothing, it is the sequence.
	const Market removed = Layered({"version: 1\ntags: ~\n", "version: 1\ntags: !append [new]\n"});
	CHECK(removed.tags == std::vector<std::string>{"new"});
}

TEST_CASE("Layers: a sequence tagged !merge= merges its elements into those below by a field")
{
	const Market market = Layered({R"(version: 1
goods: !merge=good
  - {good: beer, target: 25}
  - {good: wine, stock: 5, target: 10}
)"});
	REQUIRE(market.goods.size() == 3);
	CHECK(market.goods[0].good == "wheat-seeds");
	CHECK(market.goods[0].stock == 90);
	// Beer keeps its place and its stock, and takes the patch's target.
	CHECK(market.goods[1].good == "beer");
	CHECK(market.goods[1].stock == 55);
	CHECK(market.goods[1].target == 25);
	CHECK(market.goods[2].good == "wine");
	CHECK(market.goods[2].stock == 5);
}

TEST_CASE("Layers: patches apply in order, each over the layers below it")
{
	const Market market = Layered(
	    {"version: 1\nfeeBps: 200\ngoods: !merge=good\n  - {good: beer, target: 25}\n",
	     "version: 1\nfeeBps: 300\ngoods: !merge=good\n  - {good: beer, stock: 1}\n",
	     "version: 1\nnote: the third's\n"});
	CHECK(market.feeBps == 300);
	CHECK(market.goods[1].target == 25);
	CHECK(market.goods[1].stock == 1);
	CHECK(market.note == "the third's");
}

TEST_CASE("Layers: a patch that does not fit is refused, naming its layer")
{
	CHECK(Refusal("- not a mapping\n").find("first: ") == 0);
	CHECK(Refusal("feeBps: 200\n").find("has no version") != std::string::npos);
	CHECK(Refusal("version: 2\nfeeBps: 200\n").find("first: ") == 0);
	CHECK(Refusal("version: 1\nfeeBps: [200\n").find("first: ") == 0);
	CHECK(
	    Refusal("version: 1\ntags: !append refinery\n").find("tagged !append is a sequence") !=
	    std::string::npos);
	CHECK(
	    Refusal("version: 1\nstation: !append [x]\n").find("is over 'Ostra Highport' below it") !=
	    std::string::npos);
	CHECK(
	    Refusal("version: 1\ngoods: !merge= [{good: x}]\n").find("names the field") !=
	    std::string::npos);
	const std::string unmatched = Refusal("version: 1\ngoods: !merge=good\n  - {stock: 3}\n");
	CHECK(unmatched.find("has no 'good'") != std::string::npos);
	CHECK(unmatched.find("Market.goods[0]") != std::string::npos);
	// A misspelt field is found once the layers are merged, at its line in the patch.
	const std::string misspelt = Refusal("version: 1\n\nfeeBsp: 200\n");
	CHECK(misspelt.find("feeBsp") != std::string::npos);
	CHECK(misspelt.find("(line 3)") != std::string::npos);
}

TEST_CASE("Layers: each layer is migrated on its own before it is merged")
{
	RegisterTypes();
	YamlMigrations migrations{"LayerTests.Cargo", {2}};
	// A step edits what the data has: a patch that does not name the old field is left alone.
	migrations.Add(
	    1,
	    [](YAML::Node &root)
	    {
		    if (root["mass"])
		    {
			    root["tonnes"] = root["mass"];
			    root.remove("mass");
		    }
	    });
	const std::vector<YamlLayer> layers{
	    {"base", "version: 2\nlabel: Halcyon ore\ntonnes: 40\n"},
	    {"old mod", "version: 1\nmass: 12\n"},
	    {"older label", "version: 1\nlabel: Vesper ice\n"}};
	const entt::meta_any value = YamlArchive::ReadLayered(layers, entt::resolve<Cargo>(), migrations);
	const Cargo &cargo = value.cast<const Cargo &>();
	CHECK(cargo.tonnes == 12);
	CHECK(cargo.label == "Vesper ice");

	const std::vector<YamlLayer> later{
	    {"base", "version: 2\nlabel: x\n"}, {"from a later build", "version: 3\ntonnes: 1\n"}};
	CHECK_THROWS_WITH_AS(
	    (void)YamlArchive::ReadLayered(later, entt::resolve<Cargo>(), migrations),
	    doctest::Contains("from a later build: "), FormatError);
}
