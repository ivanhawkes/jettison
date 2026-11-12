#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Lanes.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/Types.h>
#include <jettison/ai/UtilityAi.h>

#include <jettison/physics/Components.h>
#include <jettison/reflect/Reflect.h>


namespace Jettison::Ai
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		Physics::RegisterTypes();
		RegisterType<OnLane>("Ai.OnLane")
		    .Field<&OnLane::from>("from", Attribute::Saved)
		    .Field<&OnLane::departure>("departure", Attribute::Saved)
		    .Field<&OnLane::to>("to", Attribute::Saved)
		    .Field<&OnLane::arrival>("arrival", Attribute::Saved)
		    .Field<&OnLane::arrivalStep>("arrivalStep", Attribute::Saved);
		RegisterType<EnterLane>("Ai.EnterLane")
		    .Field<&EnterLane::entity>("entity", Attribute::Saved)
		    .Field<&EnterLane::jumpPoint>("jumpPoint", Attribute::Saved);
		RegisterType<LaneEntered>("Ai.LaneEntered")
		    .Field<&LaneEntered::entity>("entity", Attribute::Saved)
		    .Field<&LaneEntered::lane>("lane", Attribute::Saved)
		    .Field<&LaneEntered::transitSeconds>("transitSeconds", Attribute::Saved);
		RegisterType<LaneLeft>("Ai.LaneLeft")
		    .Field<&LaneLeft::entity>("entity", Attribute::Saved)
		    .Field<&LaneLeft::system>("system", Attribute::Saved)
		    .Field<&LaneLeft::arrival>("arrival", Attribute::Saved);
		RegisterType<LaneRefused>("Ai.LaneRefused")
		    .Field<&LaneRefused::entity>("entity", Attribute::Saved)
		    .Field<&LaneRefused::jumpPoint>("jumpPoint", Attribute::Saved)
		    .Field<&LaneRefused::reason>("reason", Attribute::Saved);

		constexpr Attribute kDefined = Attribute::Saved | Attribute::Moddable;
		RegisterEnum<Curve>("Ai.Curve")
		    .Value<Curve::Linear>("Linear")
		    .Value<Curve::Power>("Power")
		    .Value<Curve::Logistic>("Logistic")
		    .Value<Curve::Step>("Step");
		RegisterType<ResponseCurve>("Ai.ResponseCurve", Attribute::Moddable)
		    .Field<&ResponseCurve::kind>("kind", kDefined)
		    .Field<&ResponseCurve::slope>("slope", kDefined)
		    .Field<&ResponseCurve::exponent>("exponent", kDefined)
		    .Field<&ResponseCurve::shift>("shift", kDefined)
		    .Field<&ResponseCurve::offset>("offset", kDefined)
		    .Field<&ResponseCurve::invert>("invert", kDefined);
		RegisterType<Consideration>("Ai.Consideration", Attribute::Moddable)
		    .Field<&Consideration::input>("input", kDefined)
		    .Field<&Consideration::low>("low", kDefined)
		    .Field<&Consideration::high>("high", kDefined)
		    .Field<&Consideration::curve>("curve", kDefined);
		RegisterType<UtilityAction>("Ai.UtilityAction", Attribute::Moddable)
		    .Field<&UtilityAction::name>("name", kDefined)
		    .Field<&UtilityAction::weight>("weight", kDefined)
		    .Field<&UtilityAction::considerations>("considerations", kDefined);
		RegisterType<DecisionDefinition>("Ai.DecisionDefinition", Attribute::Moddable)
		    .Field<&DecisionDefinition::actions>("actions", kDefined);

		RegisterEnum<Status>("Ai.Status")
		    .Value<Status::Running>("Running")
		    .Value<Status::Success>("Success")
		    .Value<Status::Failure>("Failure");
		RegisterEnum<NodeKind>("Ai.NodeKind")
		    .Value<NodeKind::Sequence>("Sequence")
		    .Value<NodeKind::Selector>("Selector")
		    .Value<NodeKind::Invert>("Invert")
		    .Value<NodeKind::Succeed>("Succeed")
		    .Value<NodeKind::Repeat>("Repeat")
		    .Value<NodeKind::Loop>("Loop")
		    .Value<NodeKind::Wait>("Wait")
		    .Value<NodeKind::Task>("Task");
		RegisterType<BehaviourNode>("Ai.BehaviourNode", Attribute::Moddable)
		    .Field<&BehaviourNode::kind>("kind", kDefined)
		    .Field<&BehaviourNode::task>("task", kDefined)
		    .Field<&BehaviourNode::arguments>("arguments", kDefined)
		    .Field<&BehaviourNode::count>("count", kDefined)
		    .Field<&BehaviourNode::seconds>("seconds", kDefined)
		    .Field<&BehaviourNode::children>("children", kDefined);
		RegisterType<BehaviourDefinition>("Ai.BehaviourDefinition", Attribute::Moddable)
		    .Field<&BehaviourDefinition::root>("root", kDefined);
		RegisterType<NodeState>("Ai.NodeState")
		    .Field<&NodeState::memory>("memory", Attribute::Saved)
		    .Field<&NodeState::running>("running", Attribute::Saved);
		RegisterType<Behaviour>("Ai.Behaviour")
		    .Field<&Behaviour::tree>("tree", Attribute::Saved)
		    .Field<&Behaviour::nodes>("nodes", Attribute::Saved)
		    .Field<&Behaviour::last>("last", Attribute::Saved);

		RegisterType<Steered>("Ai.Steered")
		    .Field<&Steered::lookahead>("lookahead", Attribute::Saved)
		    .Field<&Steered::clearance>("clearance", Attribute::Saved)
		    .Field<&Steered::separation>("separation", Attribute::Saved);
		RegisterType<Destination>("Ai.Destination")
		    .Field<&Destination::entity>("entity", Attribute::Saved)
		    .Field<&Destination::offset>("offset", Attribute::Saved)
		    .Field<&Destination::waypoints>("waypoints", Attribute::Saved)
		    .Field<&Destination::speed>("speed", Attribute::Saved)
		    .Field<&Destination::passRadius>("passRadius", Attribute::Saved)
		    .Field<&Destination::arrivalRadius>("arrivalRadius", Attribute::Saved)
		    .Field<&Destination::arrivalSpeed>("arrivalSpeed", Attribute::Saved)
		    .Field<&Destination::byHull>("byHull", Attribute::Saved)
		    .Field<&Destination::face>("face", Attribute::Saved)
		    .Field<&Destination::facing>("facing", Attribute::Saved)
		    .Field<&Destination::next>("next", Attribute::Saved)
		    .Field<&Destination::arrived>("arrived", Attribute::Saved);
		RegisterType<SetDestination>("Ai.SetDestination")
		    .Field<&SetDestination::ship>("ship", Attribute::Saved)
		    .Field<&SetDestination::destination>("destination", Attribute::Saved);
		RegisterType<ClearDestination>("Ai.ClearDestination")
		    .Field<&ClearDestination::ship>("ship", Attribute::Saved);
		RegisterType<Arrived>("Ai.Arrived")
		    .Field<&Arrived::ship>("ship", Attribute::Saved)
		    .Field<&Arrived::entity>("entity", Attribute::Saved);
		RegisterType<DestinationLost>("Ai.DestinationLost")
		    .Field<&DestinationLost::ship>("ship", Attribute::Saved)
		    .Field<&DestinationLost::reason>("reason", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Ai
