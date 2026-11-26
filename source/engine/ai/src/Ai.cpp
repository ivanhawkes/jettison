#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/Types.h>
#include <jettison/ai/UtilityAi.h>

#include <jettison/physics/Components.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Binary.h>


namespace Jettison::Ai
{
const Serialise::Migrations<Serialise::BinaryRecord> &Destination::Migrations()
{
	static const Serialise::Migrations<Serialise::BinaryRecord> migrations{"Ai.Destination", {2}};
	return migrations;
}


void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		Physics::RegisterTypes();
		RegisterEnum<Reason>("Ai.Reason")
		    .Value<Reason::NoSuchEntity>("no_such_entity")
		    .Value<Reason::DestinationGone>("destination_gone")
		    .Value<Reason::DestinationOtherFrame>("destination_other_frame")
		    .Value<Reason::DestinationOtherLocalFrame>("destination_other_local_frame")
		    .Value<Reason::NotController>("not_controller")
		    .Value<Reason::NoPilot>("no_pilot")
		    .Value<Reason::HelmHeld>("helm_held")
		    .Value<Reason::NotAtHelm>("not_at_helm")
		    .Value<Reason::NoSuchTree>("no_such_tree");
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
		    .Value<NodeKind::ReactiveSequence>("ReactiveSequence")
		    .Value<NodeKind::ReactiveSelector>("ReactiveSelector")
		    .Value<NodeKind::Parallel>("Parallel")
		    .Value<NodeKind::Invert>("Invert")
		    .Value<NodeKind::Succeed>("Succeed")
		    .Value<NodeKind::Repeat>("Repeat")
		    .Value<NodeKind::Loop>("Loop")
		    .Value<NodeKind::Wait>("Wait")
		    .Value<NodeKind::Task>("Task")
		    .Value<NodeKind::Subtree>("Subtree");
		RegisterType<BehaviourNode>("Ai.BehaviourNode", Attribute::Moddable)
		    .Field<&BehaviourNode::kind>("kind", kDefined)
		    .Field<&BehaviourNode::task>("task", kDefined)
		    .Field<&BehaviourNode::arguments>("arguments", kDefined)
		    .Field<&BehaviourNode::count>("count", kDefined)
		    .Field<&BehaviourNode::seconds>("seconds", kDefined)
		    .Field<&BehaviourNode::tree>("tree", kDefined)
		    .Field<&BehaviourNode::children>("children", kDefined);
		RegisterType<BehaviourDefinition>("Ai.BehaviourDefinition", Attribute::Moddable)
		    .Field<&BehaviourDefinition::root>("root", kDefined)
		    .Field<&BehaviourDefinition::interval>("interval", kDefined);
		RegisterType<NodeState>("Ai.NodeState")
		    .Field<&NodeState::memory>("memory", Attribute::Saved)
		    .Field<&NodeState::running>("running", Attribute::Saved);
		RegisterType<Behaviour>("Ai.Behaviour")
		    .Field<&Behaviour::tree>("tree", Attribute::Saved)
		    .Field<&Behaviour::nodes>("nodes", Attribute::Saved)
		    .Field<&Behaviour::last>("last", Attribute::Saved);
		RegisterType<Blackboard>("Ai.Blackboard")
		    .Field<&Blackboard::integers>("integers", Attribute::Saved)
		    .Field<&Blackboard::entities>("entities", Attribute::Saved);
		RegisterType<AssignBehaviour>("Ai.AssignBehaviour")
		    .Field<&AssignBehaviour::sender>("sender", Attribute::Saved)
		    .Field<&AssignBehaviour::entity>("entity", Attribute::Saved)
		    .Field<&AssignBehaviour::tree>("tree", Attribute::Saved);
		RegisterType<BehaviourAssigned>("Ai.BehaviourAssigned")
		    .Field<&BehaviourAssigned::sender>("sender", Attribute::Saved)
		    .Field<&BehaviourAssigned::entity>("entity", Attribute::Saved)
		    .Field<&BehaviourAssigned::tree>("tree", Attribute::Saved);
		RegisterType<BehaviourRefused>("Ai.BehaviourRefused")
		    .Field<&BehaviourRefused::sender>("sender", Attribute::Saved)
		    .Field<&BehaviourRefused::entity>("entity", Attribute::Saved)
		    .Field<&BehaviourRefused::reason>("reason", Attribute::Saved);
		RegisterType<TakeHelm>("Ai.TakeHelm")
		    .Field<&TakeHelm::sender>("sender", Attribute::Saved)
		    .Field<&TakeHelm::vehicle>("vehicle", Attribute::Saved);
		RegisterType<LeaveHelm>("Ai.LeaveHelm")
		    .Field<&LeaveHelm::sender>("sender", Attribute::Saved)
		    .Field<&LeaveHelm::vehicle>("vehicle", Attribute::Saved);
		RegisterType<HelmTaken>("Ai.HelmTaken")
		    .Field<&HelmTaken::controller>("controller", Attribute::Saved)
		    .Field<&HelmTaken::vehicle>("vehicle", Attribute::Saved);
		RegisterType<HelmLeft>("Ai.HelmLeft")
		    .Field<&HelmLeft::controller>("controller", Attribute::Saved)
		    .Field<&HelmLeft::vehicle>("vehicle", Attribute::Saved);
		RegisterType<HelmRefused>("Ai.HelmRefused")
		    .Field<&HelmRefused::sender>("sender", Attribute::Saved)
		    .Field<&HelmRefused::vehicle>("vehicle", Attribute::Saved)
		    .Field<&HelmRefused::reason>("reason", Attribute::Saved);

		RegisterType<Steered>("Ai.Steered")
		    .Field<&Steered::lookahead>("lookahead", Attribute::Saved)
		    .Field<&Steered::clearance>("clearance", Attribute::Saved)
		    .Field<&Steered::separation>("separation", Attribute::Saved);
		RegisterType<Destination>("Ai.Destination")
		    .Field<&Destination::entity>("entity", Attribute::Saved)
		    .Field<&Destination::offset>("offset", Attribute::Saved)
		    .Field<&Destination::waypoints>("waypoints", Attribute::Saved)
		    .Field<&Destination::waypointsOnEntity>("waypointsOnEntity", Attribute::Saved)
		    .Field<&Destination::speed>("speed", Attribute::Saved)
		    .Field<&Destination::passRadius>("passRadius", Attribute::Saved)
		    .Field<&Destination::arrivalRadius>("arrivalRadius", Attribute::Saved)
		    .Field<&Destination::arrivalSpeed>("arrivalSpeed", Attribute::Saved)
		    .Field<&Destination::byShape>("byShape", Attribute::Saved)
		    .Field<&Destination::face>("face", Attribute::Saved)
		    .Field<&Destination::facing>("facing", Attribute::Saved)
		    .Field<&Destination::next>("next", Attribute::Saved)
		    .Field<&Destination::arrived>("arrived", Attribute::Saved);
		RegisterType<SetDestination>("Ai.SetDestination")
		    .Field<&SetDestination::sender>("sender", Attribute::Saved)
		    .Field<&SetDestination::vehicle>("vehicle", Attribute::Saved)
		    .Field<&SetDestination::destination>("destination", Attribute::Saved);
		RegisterType<ClearDestination>("Ai.ClearDestination")
		    .Field<&ClearDestination::sender>("sender", Attribute::Saved)
		    .Field<&ClearDestination::vehicle>("vehicle", Attribute::Saved);
		RegisterType<Arrived>("Ai.Arrived")
		    .Field<&Arrived::vehicle>("vehicle", Attribute::Saved)
		    .Field<&Arrived::entity>("entity", Attribute::Saved);
		RegisterType<DestinationLost>("Ai.DestinationLost")
		    .Field<&DestinationLost::vehicle>("vehicle", Attribute::Saved)
		    .Field<&DestinationLost::reason>("reason", Attribute::Saved);
		RegisterType<DestinationRefused>("Ai.DestinationRefused")
		    .Field<&DestinationRefused::sender>("sender", Attribute::Saved)
		    .Field<&DestinationRefused::vehicle>("vehicle", Attribute::Saved)
		    .Field<&DestinationRefused::reason>("reason", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Ai
