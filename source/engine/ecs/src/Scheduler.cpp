#include <jettison/ecs/Scheduler.h>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

namespace Jettison::Ecs
{
namespace
{
constexpr std::size_t kPhaseCount = static_cast<std::size_t>(TickPhase::Render) + 1;

std::size_t Index(TickPhase phase)
{
	return static_cast<std::size_t>(phase);
}

bool Intersects(const std::vector<entt::id_type> &left, const std::vector<entt::id_type> &right)
{
	return std::ranges::any_of(
	    left, [&right](entt::id_type id) { return std::ranges::find(right, id) != right.end(); });
}

const SystemDescription *Named(const std::vector<SystemDescription> &systems, std::string_view name)
{
	const auto found = std::ranges::find(systems, name, &SystemDescription::name);
	return found == systems.end() ? nullptr : &*found;
}

bool Names(const std::vector<std::string> &names, std::string_view name)
{
	return std::ranges::find(names, name) != names.end();
}

// Whether a constraint puts the first ahead of the second: the second runs after it, or it runs
// before the second.
bool Precedes(const SystemDescription &first, const SystemDescription &second)
{
	return Names(second.after, first.name) || Names(first.before, second.name);
}
} // namespace

bool Access::ConflictsWith(const Access &other) const
{
	return Intersects(writes, other.writes) || Intersects(writes, other.reads) ||
	       Intersects(reads, other.writes);
}

void Scheduler::Register(SystemDescription system)
{
	if (system.name.empty())
		throw std::logic_error{"Scheduler: a system needs a name"};
	if (!system.run)
		throw std::logic_error{std::format("Scheduler: system '{}' has nothing to run", system.name)};
	if (Named(m_systems, system.name))
		throw std::logic_error{
		    std::format("Scheduler: a system named '{}' is already registered", system.name)};

	// Both directions: this one naming an earlier system, and an earlier one naming this.
	const auto crossPhase = [](const SystemDescription &named, const SystemDescription &naming)
	{
		return std::format(
		    "Scheduler: system '{}' runs {} '{}', which is in another phase; order across phases is "
		    "the phases' own",
		    naming.name, Names(naming.after, named.name) ? "after" : "before", named.name);
	};
	for (const SystemDescription &other : m_systems)
	{
		if (other.phase == system.phase)
			continue;
		if (Names(system.after, other.name) || Names(system.before, other.name))
			throw std::logic_error{crossPhase(other, system)};
		if (Names(other.after, system.name) || Names(other.before, system.name))
			throw std::logic_error{crossPhase(system, other)};
	}

	m_systems.push_back(std::move(system));
	try
	{
		m_order.resize(kPhaseCount);
		m_order[Index(m_systems.back().phase)] = Sort(m_systems.back().phase);
	}
	catch (...)
	{
		// A system that closes a cycle is not kept, so the scheduler stays runnable.
		m_systems.pop_back();
		throw;
	}
	m_validated = false;
}

std::vector<std::size_t> Scheduler::Sort(TickPhase phase) const
{
	// Kahn's algorithm, always taking the earliest-registered system that is ready, so with no
	// constraints the order is registration order. An after or a before naming a system not
	// registered yet is skipped here; Validate reports it if it never comes.
	std::vector<std::size_t> members;
	for (std::size_t index = 0; index < m_systems.size(); ++index)
	{
		if (m_systems[index].phase == phase)
			members.push_back(index);
	}

	std::vector<std::size_t> order;
	std::vector<bool> placed(m_systems.size(), false);
	while (order.size() < members.size())
	{
		const auto ready = std::ranges::find_if(
		    members,
		    [&](std::size_t index)
		    {
			    return !placed[index] &&
			           std::ranges::all_of(
			               members, [&](std::size_t other)
			               { return placed[other] || !Precedes(m_systems[other], m_systems[index]); });
		    });
		if (ready == members.end())
		{
			std::string cycle;
			for (const std::size_t index : members)
			{
				if (!placed[index])
					cycle += std::format("{}'{}'", cycle.empty() ? "" : ", ", m_systems[index].name);
			}
			throw std::logic_error{
			    std::format("Scheduler: the order constraints among {} form a cycle", cycle)};
		}
		placed[*ready] = true;
		order.push_back(*ready);
	}
	return order;
}

void Scheduler::Validate() const
{
	for (const SystemDescription &system : m_systems)
	{
		for (const auto &[names, relation] :
		     {std::pair{&system.after, "after"}, std::pair{&system.before, "before"}})
		{
			for (const std::string &name : *names)
			{
				if (!Named(m_systems, name))
					throw std::logic_error{std::format(
					    "Scheduler: system '{}' runs {} '{}', which is not registered", system.name,
					    relation, name)};
			}
		}
	}
	m_validated = true;
}

void Scheduler::Run(TickPhase phase, entt::registry &registry, double deltaTime) const
{
	if (!m_validated)
		Validate();
	if (m_order.empty())
		return;
	for (const std::size_t index : m_order[Index(phase)])
		m_systems[index].run(registry, deltaTime);
}

std::vector<std::string> Scheduler::Order(TickPhase phase) const
{
	std::vector<std::string> names;
	if (!m_order.empty())
	{
		for (const std::size_t index : m_order[Index(phase)])
			names.push_back(m_systems[index].name);
	}
	return names;
}

std::vector<std::pair<std::string, std::string>> Scheduler::Ambiguities(TickPhase phase) const
{
	if (m_order.empty())
		return {};
	const std::vector<std::size_t> &order = m_order[Index(phase)];

	// before[i][j]: some chain of constraints puts order[i] ahead of order[j]. Run order is a
	// topological order, so a chain only ever runs from a lower position to a higher one.
	const std::size_t count = order.size();
	std::vector<std::vector<bool>> before(count, std::vector<bool>(count, false));
	for (std::size_t later = 0; later < count; ++later)
	{
		for (std::size_t earlier = 0; earlier < later; ++earlier)
		{
			if (!Precedes(m_systems[order[earlier]], m_systems[order[later]]))
				continue;
			before[earlier][later] = true;
			for (std::size_t first = 0; first < earlier; ++first)
			{
				if (before[first][earlier])
					before[first][later] = true;
			}
		}
	}

	std::vector<std::pair<std::string, std::string>> pairs;
	for (std::size_t earlier = 0; earlier < count; ++earlier)
	{
		for (std::size_t later = earlier + 1; later < count; ++later)
		{
			const SystemDescription &first = m_systems[order[earlier]];
			const SystemDescription &second = m_systems[order[later]];
			if (!before[earlier][later] && first.access.ConflictsWith(second.access))
				pairs.emplace_back(first.name, second.name);
		}
	}
	return pairs;
}
} // namespace Jettison::Ecs
