#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Lanes.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/Types.h>
#include <jettison/ai/UtilityAi.h>

#include <jettison/core/Assert.h>
#include <jettison/physics/Components.h>
#include <jettison/reflect/Reflect.h>

#include <utility>

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

int Choose(
    const std::vector<ScoredAction> & /*unused*/, entt::registry & /*unused*/, entt::entity /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}

BehaviourTree::BehaviourTree(std::unique_ptr<Node> root) : m_pRoot{std::move(root)}
{
}
Status BehaviourTree::Tick(entt::registry & /*unused*/, entt::entity /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Ai
