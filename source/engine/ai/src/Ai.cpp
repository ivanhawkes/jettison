#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Cruise.h>
#include <jettison/ai/Gates.h>
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
		RegisterEnum<Reason>("Ai.Reason")
		    .Value<Reason::NoSuchEntity>("no_such_entity")
		    .Value<Reason::OnLane>("on_lane")
		    .Value<Reason::InNoSystem>("in_no_system")
		    .Value<Reason::NoGalaxy>("no_galaxy")
		    .Value<Reason::SystemNotInGalaxy>("system_not_in_galaxy")
		    .Value<Reason::NoLane>("no_lane")
		    .Value<Reason::ArrivalUndefined>("arrival_undefined")
		    .Value<Reason::DestinationGone>("destination_gone")
		    .Value<Reason::DestinationOtherSystem>("destination_other_system")
		    .Value<Reason::DestinationOtherFrame>("destination_other_frame")
		    .Value<Reason::NotController>("not_controller")
		    .Value<Reason::AlreadyCruising>("already_cruising")
		    .Value<Reason::CannotCruise>("cannot_cruise")
		    .Value<Reason::SystemUndefined>("system_undefined")
		    .Value<Reason::NoSuchBody>("no_such_body")
		    .Value<Reason::NearStation>("near_station")
		    .Value<Reason::AlreadyThere>("already_there")
		    .Value<Reason::NoCourse>("no_course")
		    .Value<Reason::NotAGate>("not_a_gate")
		    .Value<Reason::CannotTransit>("cannot_transit")
		    .Value<Reason::NotAtGate>("not_at_gate")
		    .Value<Reason::AlreadyCleared>("already_cleared")
		    .Value<Reason::AlreadyQueued>("already_queued")
		    .Value<Reason::LeadsNowhere>("leads_nowhere")
		    .Value<Reason::NotCleared>("not_cleared")
		    .Value<Reason::HullTooLarge>("hull_too_large")
		    .Value<Reason::NoPilot>("no_pilot")
		    .Value<Reason::HelmHeld>("helm_held")
		    .Value<Reason::NotAtHelm>("not_at_helm");
		RegisterType<OnLane>("Ai.OnLane")
		    .Field<&OnLane::from>("from", Attribute::Saved)
		    .Field<&OnLane::departure>("departure", Attribute::Saved)
		    .Field<&OnLane::to>("to", Attribute::Saved)
		    .Field<&OnLane::arrival>("arrival", Attribute::Saved)
		    .Field<&OnLane::arrivalStep>("arrivalStep", Attribute::Saved);
		RegisterType<EnterLane>("Ai.EnterLane")
		    .Field<&EnterLane::sender>("sender", Attribute::Saved)
		    .Field<&EnterLane::entity>("entity", Attribute::Saved)
		    .Field<&EnterLane::jumpPoint>("jumpPoint", Attribute::Saved);
		RegisterType<LaneEntered>("Ai.LaneEntered", Attribute::Script)
		    .Field<&LaneEntered::entity>("entity", Attribute::Saved)
		    .Field<&LaneEntered::lane>("lane", Attribute::Saved)
		    .Field<&LaneEntered::transitSeconds>("transitSeconds", Attribute::Saved);
		RegisterType<LaneLeft>("Ai.LaneLeft", Attribute::Script)
		    .Field<&LaneLeft::entity>("entity", Attribute::Saved)
		    .Field<&LaneLeft::system>("system", Attribute::Saved)
		    .Field<&LaneLeft::arrival>("arrival", Attribute::Saved);
		RegisterType<LaneRefused>("Ai.LaneRefused", Attribute::Script)
		    .Field<&LaneRefused::entity>("entity", Attribute::Saved)
		    .Field<&LaneRefused::jumpPoint>("jumpPoint", Attribute::Saved)
		    .Field<&LaneRefused::reason>("reason", Attribute::Saved);

		RegisterType<Gate>("Ai.Gate")
		    .Field<&Gate::jumpPoint>("jumpPoint", Attribute::Saved)
		    .Field<&Gate::cleared>("cleared", Attribute::Saved)
		    .Field<&Gate::lapses>("lapses", Attribute::Saved)
		    .Field<&Gate::queue>("queue", Attribute::Saved);
		RegisterType<Transit>("Ai.Transit")
		    .Field<&Transit::gate>("gate", Attribute::Saved)
		    .Field<&Transit::cleared>("cleared", Attribute::Saved);
		RegisterType<RequestTransit>("Ai.RequestTransit")
		    .Field<&RequestTransit::sender>("sender", Attribute::Saved)
		    .Field<&RequestTransit::ship>("ship", Attribute::Saved)
		    .Field<&RequestTransit::gate>("gate", Attribute::Saved);
		RegisterType<TransitCleared>("Ai.TransitCleared", Attribute::Script)
		    .Field<&TransitCleared::ship>("ship", Attribute::Saved)
		    .Field<&TransitCleared::gate>("gate", Attribute::Saved)
		    .Field<&TransitCleared::toll>("toll", Attribute::Saved)
		    .Field<&TransitCleared::lapses>("lapses", Attribute::Saved);
		RegisterType<TransitQueued>("Ai.TransitQueued", Attribute::Script)
		    .Field<&TransitQueued::ship>("ship", Attribute::Saved)
		    .Field<&TransitQueued::gate>("gate", Attribute::Saved)
		    .Field<&TransitQueued::place>("place", Attribute::Saved);
		RegisterType<TransitRefused>("Ai.TransitRefused", Attribute::Script)
		    .Field<&TransitRefused::ship>("ship", Attribute::Saved)
		    .Field<&TransitRefused::gate>("gate", Attribute::Saved)
		    .Field<&TransitRefused::reason>("reason", Attribute::Saved);
		RegisterType<TransitLapsed>("Ai.TransitLapsed", Attribute::Script)
		    .Field<&TransitLapsed::ship>("ship", Attribute::Saved)
		    .Field<&TransitLapsed::gate>("gate", Attribute::Saved);
		RegisterType<TransitSent>("Ai.TransitSent", Attribute::Script)
		    .Field<&TransitSent::ship>("ship", Attribute::Saved)
		    .Field<&TransitSent::gate>("gate", Attribute::Saved)
		    .Field<&TransitSent::toll>("toll", Attribute::Saved);
		RegisterType<GatePassed>("Ai.GatePassed", Attribute::Script)
		    .Field<&GatePassed::ship>("ship", Attribute::Saved)
		    .Field<&GatePassed::gate>("gate", Attribute::Saved)
		    .Field<&GatePassed::reason>("reason", Attribute::Saved);

		constexpr Attribute kDefined = Attribute::Saved | Attribute::Moddable;
		RegisterType<CruiseDrive>("Ai.CruiseDrive", Attribute::Moddable)
		    .Field<&CruiseDrive::seconds>("seconds", kDefined)
		    .Field<&CruiseDrive::reference>("reference", kDefined)
		    .Field<&CruiseDrive::exponent>("exponent", kDefined);
		RegisterType<Cruising>("Ai.Cruising")
		    .Field<&Cruising::target>("target", Attribute::Saved)
		    .Field<&Cruising::frame>("frame", Attribute::Saved)
		    .Field<&Cruising::carrier>("carrier", Attribute::Saved)
		    .Field<&Cruising::course>("course", Attribute::Saved)
		    .Field<&Cruising::departureStep>("departureStep", Attribute::Saved)
		    .Field<&Cruising::arrivalStep>("arrivalStep", Attribute::Saved)
		    .Field<&Cruising::body>("body", Attribute::Saved);
		RegisterType<Cruise>("Ai.Cruise")
		    .Field<&Cruise::sender>("sender", Attribute::Saved)
		    .Field<&Cruise::ship>("ship", Attribute::Saved)
		    .Field<&Cruise::body>("body", Attribute::Saved);
		RegisterType<CruiseStarted>("Ai.CruiseStarted", Attribute::Script)
		    .Field<&CruiseStarted::ship>("ship", Attribute::Saved)
		    .Field<&CruiseStarted::body>("body", Attribute::Saved)
		    .Field<&CruiseStarted::seconds>("seconds", Attribute::Saved)
		    .Field<&CruiseStarted::metres>("metres", Attribute::Saved)
		    .Field<&CruiseStarted::arrivalStep>("arrivalStep", Attribute::Saved);
		RegisterType<CruiseEnded>("Ai.CruiseEnded", Attribute::Script)
		    .Field<&CruiseEnded::ship>("ship", Attribute::Saved)
		    .Field<&CruiseEnded::body>("body", Attribute::Saved);
		RegisterType<CruiseRefused>("Ai.CruiseRefused", Attribute::Script)
		    .Field<&CruiseRefused::ship>("ship", Attribute::Saved)
		    .Field<&CruiseRefused::body>("body", Attribute::Saved)
		    .Field<&CruiseRefused::reason>("reason", Attribute::Saved);
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
		RegisterType<TakeHelm>("Ai.TakeHelm")
		    .Field<&TakeHelm::sender>("sender", Attribute::Saved)
		    .Field<&TakeHelm::ship>("ship", Attribute::Saved);
		RegisterType<LeaveHelm>("Ai.LeaveHelm")
		    .Field<&LeaveHelm::sender>("sender", Attribute::Saved)
		    .Field<&LeaveHelm::ship>("ship", Attribute::Saved);
		RegisterType<HelmTaken>("Ai.HelmTaken")
		    .Field<&HelmTaken::controller>("controller", Attribute::Saved)
		    .Field<&HelmTaken::ship>("ship", Attribute::Saved);
		RegisterType<HelmLeft>("Ai.HelmLeft")
		    .Field<&HelmLeft::controller>("controller", Attribute::Saved)
		    .Field<&HelmLeft::ship>("ship", Attribute::Saved);
		RegisterType<HelmRefused>("Ai.HelmRefused")
		    .Field<&HelmRefused::sender>("sender", Attribute::Saved)
		    .Field<&HelmRefused::ship>("ship", Attribute::Saved)
		    .Field<&HelmRefused::reason>("reason", Attribute::Saved);

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
		    .Field<&SetDestination::sender>("sender", Attribute::Saved)
		    .Field<&SetDestination::ship>("ship", Attribute::Saved)
		    .Field<&SetDestination::destination>("destination", Attribute::Saved);
		RegisterType<ClearDestination>("Ai.ClearDestination")
		    .Field<&ClearDestination::sender>("sender", Attribute::Saved)
		    .Field<&ClearDestination::ship>("ship", Attribute::Saved);
		RegisterType<Arrived>("Ai.Arrived")
		    .Field<&Arrived::ship>("ship", Attribute::Saved)
		    .Field<&Arrived::entity>("entity", Attribute::Saved);
		RegisterType<DestinationLost>("Ai.DestinationLost")
		    .Field<&DestinationLost::ship>("ship", Attribute::Saved)
		    .Field<&DestinationLost::reason>("reason", Attribute::Saved);
		RegisterType<DestinationRefused>("Ai.DestinationRefused")
		    .Field<&DestinationRefused::sender>("sender", Attribute::Saved)
		    .Field<&DestinationRefused::ship>("ship", Attribute::Saved)
		    .Field<&DestinationRefused::reason>("reason", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace Jettison::Ai
