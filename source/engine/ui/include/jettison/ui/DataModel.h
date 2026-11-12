#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace Jettison::Ui
{
// A value a document hands an event: data-event-click="buy(good.id, 10)" calls "buy" with the
// good's id as text and 10 as a whole number.
using EventArgument = std::variant<bool, std::int64_t, double, std::string>;

// The argument as a whole number, if it is one or a number with no fraction; 0 otherwise.
std::int64_t AsInteger(const EventArgument &argument);
// The argument as text, a number written as a document would write it.
std::string AsText(const EventArgument &argument);

namespace Detail
{
class DataModelState;
}

// A document's view of the game's data (docs/architecture.md, "UI"): named variables a document
// reads with {{market.goods[0].ask}} and data-for="good : market.goods", and named events it calls.
//
// A variable is a value presentation owns, bound by address, of any type registered with Reflect or
// any type an archive writes as a leaf (bool, the integers, float, double, std::string, a registered
// enumeration by its name) or a std::vector of one of those. Its fields are found through the
// reflection tables and read in place, so binding a type needs no code beyond its registration. The
// values are presentation's copies of what snapshots carry, never the simulation's own state:
// presentation copies them when a step brings new ones and says so with Changed, and the documents
// read them again as they are next updated. An event turns a document's action into what the game
// does with it, which for the simulation is a command.
//
// Made by UiSystem::CreateDataModel, before the documents that read it load; it lasts as long as the
// UI.
class DataModel
{
  public:
	explicit DataModel(std::unique_ptr<Detail::DataModelState> pState);
	~DataModel();
	DataModel(const DataModel &) = delete;
	DataModel &operator=(const DataModel &) = delete;

	const std::string &Name() const;

	// Binds the value under the name. The value outlives the model. Throws std::logic_error for a
	// name bound twice, or a type the model cannot show (one Reflect does not know, a field of such a
	// type), naming it, so a binding that cannot work fails at start-up rather than as an empty
	// screen.
	template <typename T> void Bind(std::string_view name, T *pValue)
	{
		BindValue(name, entt::resolve<T>(), static_cast<void *>(pValue));
	}

	// Calls the function when a document raises the event of that name. Throws std::logic_error for
	// a name bound twice.
	void BindEvent(std::string_view name, std::function<void(std::span<const EventArgument>)> handler);

	// The variable's value has moved: what reads it is updated as the UI next updates.
	void Changed(std::string_view name);
	void ChangedAll();

  private:
	void BindValue(std::string_view name, const entt::meta_type &type, void *pValue);

	std::unique_ptr<Detail::DataModelState> m_pState;
};
} // namespace Jettison::Ui
