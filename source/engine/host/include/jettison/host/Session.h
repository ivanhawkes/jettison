#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/host/Package.h>
#include <jettison/vfs/Mount.h>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
} // namespace Jettison::Sim

namespace Jettison::Host
{
// The start-up phases, in the order a session runs them.
enum class Phase
{
	Options,
	Mount,
	Types,
	Content,
	World,
	InitialState,
};

std::string_view NameOf(Phase phase);

// A start-up that failed: the phase, the content source it was reading (empty when none) and the
// cause, all in what().
class StartupError : public std::runtime_error
{
  public:
	StartupError(Phase phase, std::filesystem::path source, std::string_view cause);

	Phase FailedPhase() const
	{
		return m_phase;
	}

	const std::filesystem::path &Source() const
	{
		return m_source;
	}

  private:
	Phase m_phase;
	std::filesystem::path m_source;
};

// A content source as mounted, with the description it carries, if any.
struct Package
{
	std::string mount;
	std::optional<PackageDescription> description;
};

// Each source of the file system as a package, in mount order, checked as a session's content phase
// checks them before the game's hook: a package for a newer engine, or one requiring a package that
// is not mounted before it, is refused. For a tool that loads a game's content with no world, such
// as a validator. Throws StartupError for the content phase, naming the source.
std::vector<Package> DescribePackages(const Vfs::FileSystem &files);

// What a game composes into a session: one hook a phase, each called once, in phase order. A hook
// throws to fail start-up; no later hook is called after one has thrown. The session owns the game
// and destroys it after the world, so state a world callback captures from the game outlives it.
class Game
{
  public:
	Game() = default;

	virtual ~Game() = default;

	Game(const Game &) = delete;

	Game &operator=(const Game &) = delete;

	// Reflection registration, before any data is decoded. Reflection is process-wide and is not
	// cleared between sessions, so this must be safe to run again for a second session.
	virtual void RegisterTypes()
	{
	}

	// Checks the packages against the game's own policy (identity, required files) and loads its
	// definitions.
	virtual void LoadContent(
	    const Vfs::FileSystem & /*files*/, std::span<const Package> /*packages*/,
	    Sim::DefinitionRegistry & /*definitions*/)
	{
	}

	// Components, services, commands and systems, in an order that is the save's order.
	virtual void RegisterWorld(
	    Sim::SimWorld & /*world*/, const Sim::DefinitionRegistry & /*definitions*/)
	{
	}

	virtual void CreateInitialState(Sim::SimWorld & /*world*/)
	{
	}
};

struct Options
{
	// Mounted read-only in this order, a later source shadowing an earlier one. At least one; a
	// writable mount is refused. A package's required packages are matched against the names of
	// the sources before it.
	std::vector<Vfs::Mount> content;
	// Seconds.
	double fixedStep{};
	std::uint64_t seed{};
};

// The owner of one game's file system, definitions and world. Constructing a session starts it: a
// constructed session is ready. On a failed start the constructor throws StartupError, having
// released everything it acquired. Shutdown destroys the world, then the definitions, then the file
// system, then the game, since each holds references into those before it.
class Session
{
  public:
	Session(Options options, std::unique_ptr<Game> game);

	~Session();

	Session(const Session &) = delete;

	Session &operator=(const Session &) = delete;

	// Idempotent. The accessors below must not be called after it.
	void Shutdown() noexcept;

	bool Running() const;

	const Vfs::FileSystem &Files() const;

	std::span<const Package> Packages() const;

	const Sim::DefinitionRegistry &Definitions() const;

	Sim::SimWorld &World();

	Game &GetGame();

  private:
	// Declared in the order they are built: a constructor that throws destroys them in reverse, the
	// order Shutdown keeps.
	std::unique_ptr<Game> m_pGame;
	std::unique_ptr<Vfs::FileSystem> m_pFiles;
	std::vector<Package> m_packages;
	std::unique_ptr<Sim::DefinitionRegistry> m_pDefinitions;
	std::unique_ptr<Sim::SimWorld> m_pWorld;
};
} // namespace Jettison::Host
