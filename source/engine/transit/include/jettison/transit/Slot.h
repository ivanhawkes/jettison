#pragma once

#include <entt/entt.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>

namespace Jettison::Transit
{
// A slot, any game's (docs/architecture.md, "Transit"): what one entity holds at a time, a car
// park's bay or a toll booth's lane, until it gives it up or its hold lapses, and the entities
// waiting for it, first come first. A gated portal is a slot with a sensor: its holder touching the
// sensor goes through, and any other body that touches it passes, unsent (Crossed). What a hold
// asks of its holder, and what going through does, are the game's. A game keeps its slots and
// claims in Slot and Claim, or in components of its own derived from them that save fields of their
// own; the parts below take either, as Kept and Held.

// A slot: the entity holding it and the step its hold lapses at (Time::SimClock::Step), and those
// waiting for it, first come first. Free while none holds it and none waits. Registered with
// Reflect, every field Saved, by RegisterTypes.
struct Slot
{
	entt::entity holder{entt::null};
	std::uint64_t lapses{0};
	std::vector<entt::entity> queue;

	bool operator==(const Slot &) const = default;
};

// An entity's business with a slot: waiting for it, or holding it. Registered with Reflect, every
// field Saved, by RegisterTypes.
struct Claim
{
	entt::entity slot{entt::null};
	bool held{false};

	bool operator==(const Claim &) const = default;
};

// The entities of a component in entity order, so what is done to them is the same however the
// storage happens to be laid out, as it is not after a load.
template <typename Component> std::vector<entt::entity> InOrder(const entt::registry &registry)
{
	const auto view = registry.view<const Component>();
	std::vector<entt::entity> entities{view.begin(), view.end()};
	std::ranges::sort(entities, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	return entities;
}

// Whether none holds the slot and none waits for it.
inline bool Free(const Slot &slot)
{
	return slot.holder == entt::null && slot.queue.empty();
}

// Gives the slot to the entity until the step its hold lapses at, its claim held.
template <typename Kept, typename Held>
void Hold(entt::registry &registry, entt::entity slot, entt::entity entity, std::uint64_t lapses)
{
	Kept &state = registry.get<Kept>(slot);
	state.holder = entity;
	state.lapses = lapses;
	Held claim;
	claim.slot = slot;
	claim.held = true;
	registry.emplace_or_replace<Held>(entity, std::move(claim));
}

// Puts the entity at the back of the slot's queue, its claim waiting. Returns its place, from 1.
template <typename Kept, typename Held>
std::uint32_t Wait(entt::registry &registry, entt::entity slot, entt::entity entity)
{
	Kept &state = registry.get<Kept>(slot);
	state.queue.push_back(entity);
	Held claim;
	claim.slot = slot;
	registry.emplace<Held>(entity, std::move(claim));
	return static_cast<std::uint32_t>(state.queue.size());
}

// Takes the entity's claim away: its hold, freeing the slot, or its place. Nothing for one with none.
template <typename Kept, typename Held> void GiveUp(entt::registry &registry, entt::entity entity)
{
	const auto *claim = registry.try_get<Held>(entity);
	if (!claim)
		return;
	if (auto *slot = registry.valid(claim->slot) ? registry.try_get<Kept>(claim->slot) : nullptr)
	{
		if (slot->holder == entity)
			slot->holder = entt::null;
		std::erase(slot->queue, entity);
	}
	registry.remove<Held>(entity);
}

// A slot's holder or waiting entity, as (entity, slot).
using Claimant = std::pair<entt::entity, entt::entity>;

// Takes from the slots, in the order given, the holders and waiting entities that no longer exist,
// and returns them, each slot's waiting first.
template <typename Kept>
std::vector<Claimant> DropGone(entt::registry &registry, std::span<const entt::entity> slots)
{
	std::vector<Claimant> gone;
	const auto absent = [&](entt::entity entity) { return !registry.valid(entity); };
	for (const entt::entity slot : slots)
	{
		Kept &state = registry.get<Kept>(slot);
		for (const entt::entity entity : state.queue)
			if (absent(entity))
				gone.emplace_back(entity, slot);
		std::erase_if(state.queue, absent);
		if (state.holder != entt::null && absent(state.holder))
		{
			gone.emplace_back(state.holder, slot);
			state.holder = entt::null;
		}
	}
	return gone;
}

// Gives up each hold of the slots, in the order given, that lapses as the step ending at that count
// (Time::SimClock::Step) ends, and returns them.
template <typename Kept, typename Held>
std::vector<Claimant> Lapse(
    entt::registry &registry, std::span<const entt::entity> slots, std::uint64_t ending)
{
	std::vector<Claimant> lapsed;
	for (const entt::entity slot : slots)
		if (const Kept &state = registry.get<Kept>(slot);
		    state.holder != entt::null && state.lapses <= ending)
		{
			const entt::entity holder = state.holder;
			GiveUp<Kept, Held>(registry, holder);
			lapsed.emplace_back(holder, slot);
		}
	return lapsed;
}

// Offers a slot none holds to its queue from the front, until one holds it or none waits: each
// entity offered leaves the queue, and offer(entity) gives it the slot (Hold) or turns it away.
template <typename Kept, typename Held, typename Offer>
void Serve(entt::registry &registry, entt::entity slot, Offer &&offer)
{
	while (registry.get<Kept>(slot).holder == entt::null && !registry.get<Kept>(slot).queue.empty())
	{
		const entt::entity entity = registry.get<Kept>(slot).queue.front();
		GiveUp<Kept, Held>(registry, entity);
		offer(entity);
	}
}

// The first step a slot changes in by itself, for a slot system's quiet (Sim::Quiet::next): this
// one for a holder or a waiting entity gone, or a queue to serve, or else the step before the first
// hold lapses, which lapses as that step ends.
template <typename Kept> std::uint64_t NextLapse(const Sim::SimWorld &world)
{
	const entt::registry &registry = world.Registry();
	const std::uint64_t now = world.Clock().Step();
	std::uint64_t next = std::numeric_limits<std::uint64_t>::max();
	for (const auto [slot, state] : registry.view<const Kept>().each())
	{
		const auto gone = [&](entt::entity entity) { return !registry.valid(entity); };
		if (std::ranges::any_of(state.queue, gone) ||
		    (state.holder == entt::null ? !state.queue.empty() : gone(state.holder)))
			return now;
		if (state.holder != entt::null)
			next = std::min(next, state.lapses - std::min<std::uint64_t>(state.lapses, 1));
	}
	return next;
}

// What went through gated portals in the last physics step, as (slot, body): each slot's holder
// touching the slot's sensor (Physics::PhysicsWorld::Touching), one already inside it included, in
// the slots' order; and every other body that began to touch a slot's sensor, in the contacts'.
struct Crossings
{
	std::vector<std::pair<entt::entity, entt::entity>> through;
	std::vector<std::pair<entt::entity, entt::entity>> passed;
};

template <typename Kept>
Crossings Crossed(
    const entt::registry &registry, const Physics::PhysicsWorld &physics,
    std::span<const entt::entity> slots)
{
	Crossings crossings;
	for (const entt::entity slot : slots)
		if (const entt::entity holder = registry.get<Kept>(slot).holder;
		    holder != entt::null && std::ranges::binary_search(physics.Touching(slot), holder))
			crossings.through.emplace_back(slot, holder);
	for (const Physics::ContactEvent &contact : physics.Contacts())
	{
		if (contact.phase != Physics::ContactPhase::Began || !contact.sensor)
			continue;
		const bool firstIsSlot = registry.valid(contact.first) && registry.all_of<Kept>(contact.first);
		const entt::entity slot = firstIsSlot ? contact.first : contact.second;
		const entt::entity body = firstIsSlot ? contact.second : contact.first;
		if (!registry.valid(slot) || !registry.all_of<Kept>(slot) ||
		    std::ranges::find(crossings.through, std::pair{slot, body}) != crossings.through.end())
			continue;
		crossings.passed.emplace_back(slot, body);
	}
	return crossings;
}
} // namespace Jettison::Transit
