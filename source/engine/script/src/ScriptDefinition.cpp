#include <jettison/script/ScriptDefinition.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/DefinitionRegistry.h>

namespace Jettison::Script
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		constexpr Attribute kDefined = Attribute::Saved | Attribute::Moddable;
		RegisterType<EventHandler>("Script.EventHandler", Attribute::Moddable)
		    .Field<&EventHandler::event>("event", kDefined)
		    .Field<&EventHandler::function>("function", kDefined);
		RegisterType<InputFunction>("Script.InputFunction", Attribute::Moddable)
		    .Field<&InputFunction::input>("input", kDefined)
		    .Field<&InputFunction::function>("function", kDefined);
		RegisterType<ScriptDefinition>("Script.ScriptDefinition", Attribute::Moddable)
		    .Field<&ScriptDefinition::module>("module", kDefined)
		    .Field<&ScriptDefinition::fuel>("fuel", kDefined)
		    .Field<&ScriptDefinition::memoryPages>("memoryPages", kDefined)
		    .Field<&ScriptDefinition::capabilities>("capabilities", kDefined)
		    .Field<&ScriptDefinition::events>("events", kDefined)
		    .Field<&ScriptDefinition::inputs>("inputs", kDefined);
		return true;
	}();
	(void)registered;
}

void LoadDefinitions(Sim::DefinitionRegistry &definitions)
{
	RegisterTypes();
	definitions.Load<ScriptDefinition>(kScriptDirectory, Serialise::FormatVersion{kScriptVersion});
}
} // namespace Jettison::Script
