// A game's smallest build with nothing of space in it: a race along a track, composed into the
// public host and run through its whole life: definitions, commands, fixed steps, a save, a load
// and teardown. The world holds one component, Token, and the game registers no transform, galaxy
// or star system. It exits non-zero at the first check that fails.
#include <jettison/core/Assert.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/host/Session.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/CommandQueue.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace Jettison;

namespace
{
// What a piece is, from the content's files.
struct Piece
{
	std::string displayName;
	int stride{};
};

// A piece on the track; the save refers to its definition by name.
struct Token
{
	std::string piece;
	int square{};
};

struct Place
{
	std::string piece;
};


class Race : public Host::Game
{
  public:
	// Reflection is process-wide, so a second session finds the types already registered.
	void RegisterTypes() override
	{
		static const bool registered = []
		{
			using Reflect::Attribute;
			Reflect::RegisterType<Piece>("Consumer.Piece")
			    .Field<&Piece::displayName>("displayName", Attribute::Saved)
			    .Field<&Piece::stride>("stride", Attribute::Saved);
			Reflect::RegisterType<Token>("Consumer.Token")
			    .Field<&Token::piece>("piece", Attribute::Saved)
			    .Field<&Token::square>("square", Attribute::Saved);
			Reflect::RegisterType<Place>("Consumer.Place")
			    .Field<&Place::piece>("piece", Attribute::Saved);
			return true;
		}();
		(void)registered;
	}


	void LoadContent(
	    const Vfs::FileSystem & /*files*/, std::span<const Host::Package> /*packages*/,
	    Sim::DefinitionRegistry &definitions) override
	{
		definitions.Load<Piece>("defs/pieces", {1});
	}


	void RegisterWorld(Sim::SimWorld &world, const Sim::DefinitionRegistry &definitions) override
	{
		world.RegisterComponent<Token>();
		world.RegisterCommand<Place>(
		    [&definitions](Sim::SimWorld &target, const Place &command)
		    {
			    JETTISON_VERIFY(definitions.Find<Piece>(command.piece) != nullptr);
			    target.Registry().emplace<Token>(target.Registry().create(), command.piece, 0);
		    });
		// A piece goes its stride, and a die the world's generator rolls may add a square.
		world.RegisterSystem(
		    {"advance",
		     Ecs::TickPhase::Simulation,
		     Ecs::Access::Of<>().Writing<Token>(),
		     {},
		     [&definitions](entt::registry &registry, double)
		     {
			     auto &random = registry.ctx().get<Math::Random>();
			     for (auto [entity, token] : registry.view<Token>().each())
				     token.square += definitions.Find<Piece>(token.piece)->stride + random.Range(0, 1);
		     }});
	}
};


std::unique_ptr<Host::Session> Start()
{
	return std::make_unique<Host::Session>(
	    Host::Options{{{"race", CONSUMER_CONTENT}}, 1.0, 7}, std::make_unique<Race>());
}


bool Expect(bool condition, const char *what)
{
	if (!condition)
		std::fprintf(stderr, "consumer: %s\n", what);
	return condition;
}


void Run(Sim::SimWorld &world, int steps)
{
	for (int step = 0; step < steps; ++step)
		world.Step();
}


int Play()
{
	const std::unique_ptr<Host::Session> pFirst = Start();
	Sim::SimWorld &world = pFirst->World();
	if (!Expect(pFirst->Definitions().Find<Piece>("hare")->stride == 3, "the definition did not load"))
		return EXIT_FAILURE;

	world.Commands().Push(Place{"hare"});
	world.Commands().Push(Place{"tortoise"});
	Run(world, 5);
	const std::vector<std::byte> save = world.Save();
	const std::uint64_t hashAtSave = world.StateHash();
	Run(world, 5);
	const std::uint64_t hashAtEnd = world.StateHash();

	// The same seed and commands give the same state in another session.
	const std::unique_ptr<Host::Session> pSecond = Start();
	pSecond->World().Commands().Push(Place{"hare"});
	pSecond->World().Commands().Push(Place{"tortoise"});
	Run(pSecond->World(), 10);

	// A third loads the save and carries on from the same point.
	const std::unique_ptr<Host::Session> pThird = Start();
	pThird->World().Load(save);
	if (!Expect(pThird->World().StateHash() == hashAtSave, "a load did not restore the state"))
		return EXIT_FAILURE;
	Run(pThird->World(), 5);

	if (!Expect(world.Registry().view<Token>().size() == 2, "the commands placed no pieces") ||
	    !Expect(pSecond->World().StateHash() == hashAtEnd, "the same run gave another state") ||
	    !Expect(pThird->World().StateHash() == hashAtEnd, "the loaded run gave another state"))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
} // namespace


int main()
{
	try
	{
		// The sessions are gone, world before game, when Play returns.
		if (Play() != EXIT_SUCCESS)
			return EXIT_FAILURE;
	}
	catch (const std::exception &error)
	{
		std::fprintf(stderr, "consumer: %s\n", error.what());
		return EXIT_FAILURE;
	}
	std::puts("consumer: a race defined, stepped, saved, loaded and torn down through Jettison::Host");
	return EXIT_SUCCESS;
}
