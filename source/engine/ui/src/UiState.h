#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataVariable.h>

#include <entt/entt.hpp>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <jettison/render/Overlay.h>
#include <jettison/ui/DataModel.h>
#include <jettison/ui/UiSystem.h>

#include "RmlInterfaces.h"

namespace Rml
{
class Context;
class ElementDocument;
} // namespace Rml

namespace Jettison::Ui::Detail
{
// How a type is shown to a document: a leaf as a value, a registered class by its fields, a sequence
// by its elements.
enum class Kind : std::uint8_t
{
	Bool,
	Signed,
	Unsigned,
	Float,
	Double,
	String,
	Enum,
	Class,
	Sequence,
};

// RmlUi's view of one type, read through the reflection tables at the address RmlUi holds.
class ReflectedDefinition;

// The definitions a model's variables use, one a type, made the first time a variable reaches it and
// kept as long as the model, since RmlUi holds them by pointer.
class Definitions
{
  public:
	// Throws std::logic_error naming the path for a type that cannot be shown, or that has a field
	// that cannot be.
	Rml::VariableDefinition *For(const entt::meta_type &type, const std::string &path);

  private:
	std::map<entt::id_type, std::unique_ptr<Rml::VariableDefinition>> m_definitions;
};

class DataModelState
{
  public:
	DataModelState(std::string name, Rml::DataModelConstructor constructor)
	    : name{std::move(name)}, constructor{constructor}, handle{constructor.GetModelHandle()}
	{
	}

	std::string name;
	Rml::DataModelConstructor constructor;
	Rml::DataModelHandle handle;
	Definitions definitions;
	std::set<std::string, std::less<>> bound;
};

// The transforms every document may apply to a value, {{credits | thousands}}, in the string table's
// language. RmlUi keeps them for every model of the context at once, so they are registered with the
// first model made; the table outlives them.
void RegisterTransforms(Rml::DataModelConstructor &constructor, const StringTable &strings);

struct DocumentSlot
{
	Rml::ElementDocument *pDocument{nullptr};
	std::uint32_t generation{0};
};

// Everything a UiSystem holds, in the order it is made; RmlUi is shut down before any of it goes.
class UiState
{
  public:
	explicit UiState(const Vfs::FileSystem &files)
	    : files{files}, fileInterface{files}, render{overlay, files}
	{
	}

	const Vfs::FileSystem &files;
	Render::Overlay overlay;
	FileInterface fileInterface;
	SystemInterface system;
	RenderInterface render;
	Rml::Context *pContext{nullptr};
	std::vector<DocumentSlot> documents;
	std::map<std::string, std::unique_ptr<DataModel>, std::less<>> models;

	Rml::ElementDocument *Find(DocumentHandle document) const;
};
} // namespace Jettison::Ui::Detail
