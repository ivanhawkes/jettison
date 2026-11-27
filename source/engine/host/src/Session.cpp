#include <jettison/host/Session.h>

#include <jettison/core/Assert.h>
#include <jettison/core/Log.h>
#include <jettison/core/Version.h>
#include <jettison/script/ScriptDefinition.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/vfs/FileSystem.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <utility>

namespace Jettison::Host
{
namespace
{
constexpr Core::Category kHost{"host"};


std::string Message(Phase phase, const std::filesystem::path &source, std::string_view cause)
{
	return source.empty() ? std::format("Start-up failed in the {} phase: {}", NameOf(phase), cause)
	                      : std::format(
	                            "Start-up failed in the {} phase, at {}: {}", NameOf(phase),
	                            source.string(), cause);
}


// Runs one phase's step, turning what it throws into a StartupError for the phase.
template <typename Step> void Run(Phase phase, const std::filesystem::path &source, Step &&step)
{
	try
	{
		std::forward<Step>(step)();
	}
	catch (const StartupError &)
	{
		throw;
	}
	catch (const std::exception &problem)
	{
		throw StartupError{phase, source, problem.what()};
	}
}


void CheckOptions(const Options &options)
{
	if (options.content.empty())
		throw StartupError{Phase::Options, {}, "no content source is given"};
	for (const Vfs::Mount &mount : options.content)
		if (mount.writable)
			throw StartupError{
			    Phase::Options, mount.source,
			    std::format(
			        "the content source '{}' is writable; content is mounted read-only", mount.name)};
	if (!std::isfinite(options.fixedStep) || options.fixedStep <= 0.0)
		throw StartupError{
		    Phase::Options, {}, std::format("a fixed step of {} s is not a step", options.fixedStep)};
}


Package Describe(const Vfs::Mount &mount, std::span<const Package> before)
{
	Package package{mount.name, DescribePackage(mount.source)};
	if (!package.description)
		return package;
	const PackageDescription &description = *package.description;
	if (!description.engine.empty() && !EngineAtLeast(Core::EngineVersion(), description.engine))
		throw std::runtime_error{std::format(
		    "it requires engine {}, newer than this build's {}", description.engine,
		    Core::EngineVersion())};
	for (const std::string &required : description.required)
		if (std::ranges::find(before, required, &Package::mount) == before.end())
			throw std::runtime_error{
			    std::format("it requires the package '{}', which is not a source before it", required)};
	CheckSide(description, mount.source);
	return package;
}
} // namespace


std::vector<Package> DescribePackages(const Vfs::FileSystem &files)
{
	std::vector<Package> packages;
	for (const Vfs::Mount &mount : files.Mounts())
		Run(Phase::Content, mount.source, [&] { packages.push_back(Describe(mount, packages)); });
	return packages;
}


std::string_view NameOf(Phase phase)
{
	switch (phase)
	{
		case Phase::Options: return "options";
		case Phase::Mount: return "mount";
		case Phase::Types: return "types";
		case Phase::Content: return "content";
		case Phase::World: return "world";
		case Phase::Scripts: return "scripts";
		case Phase::InitialState: return "initial state";
	}
	return "unknown";
}


StartupError::StartupError(Phase phase, std::filesystem::path source, std::string_view cause)
    : std::runtime_error{Message(phase, source, cause)}, m_phase{phase}, m_source{std::move(source)}
{
}


Session::Session(Options options, std::unique_ptr<Game> game) : m_pGame{std::move(game)}
{
	if (!m_pGame)
		throw StartupError{Phase::Options, {}, "no game is given"};
	CheckOptions(options);

	m_pFiles = std::make_unique<Vfs::FileSystem>();
	for (const Vfs::Mount &mount : options.content)
		Run(Phase::Mount, mount.source, [&] { m_pFiles->AddMount(mount); });

	Run(Phase::Types, {},
	    [&]
	    {
		    RegisterPackageTypes();
		    m_pGame->RegisterTypes();
	    });

	if (options.server)
		LeaveOutClientPackages();
	m_packages = DescribePackages(*m_pFiles);
	m_pDefinitions = std::make_unique<Sim::DefinitionRegistry>(*m_pFiles);
	Run(Phase::Content, {},
	    [&]
	    {
		    m_pGame->LoadContent(*m_pFiles, m_packages, *m_pDefinitions);
		    if (options.scripts)
			    Script::LoadDefinitions(*m_pDefinitions);
	    });

	Run(Phase::World, {},
	    [&]
	    {
		    m_pWorld = std::make_unique<Sim::SimWorld>(options.fixedStep, options.seed);
		    m_pGame->RegisterWorld(*m_pWorld, *m_pDefinitions);
	    });

	// After the world, whose commands a script's grants name; a script that does not load is logged
	// and left out, as a mod that cannot be mounted is.
	if (options.scripts)
		Run(Phase::Scripts, {},
		    [&]
		    {
			    m_pScripts = std::make_unique<Script::ScriptHost>(*m_pFiles, *options.scripts);
			    m_pScripts->SendCommandsTo(*m_pWorld, m_pGame->ScriptSender());
			    m_pScripts->LoadAll(*m_pDefinitions);
		    });

	Run(Phase::InitialState, {}, [&] { m_pGame->CreateInitialState(*m_pWorld); });
}


Session::~Session()
{
	Shutdown();
}


void Session::LeaveOutClientPackages()
{
	auto kept = std::make_unique<Vfs::FileSystem>();
	for (const Vfs::Mount &mount : m_pFiles->Mounts())
	{
		std::optional<PackageDescription> description;
		Run(Phase::Content, mount.source, [&] { description = DescribePackage(mount.source); });
		if (description && description->side == Side::Client)
			Core::Log(
			    Core::Severity::Info, kHost, "{} is for the client alone and left out of the server",
			    mount.name);
		else
			kept->AddMount(mount);
	}
	m_pFiles = std::move(kept);
}


void Session::Shutdown() noexcept
{
	m_pScripts.reset();
	m_pWorld.reset();
	m_pDefinitions.reset();
	m_packages.clear();
	m_pFiles.reset();
	m_pGame.reset();
}


bool Session::Running() const
{
	return m_pWorld != nullptr;
}


const Vfs::FileSystem &Session::Files() const
{
	JETTISON_ASSERT(m_pFiles);
	return *m_pFiles;
}


std::span<const Package> Session::Packages() const
{
	return m_packages;
}


const Sim::DefinitionRegistry &Session::Definitions() const
{
	JETTISON_ASSERT(m_pDefinitions);
	return *m_pDefinitions;
}


Sim::SimWorld &Session::World()
{
	JETTISON_ASSERT(m_pWorld);
	return *m_pWorld;
}


Game &Session::GetGame()
{
	JETTISON_ASSERT(m_pGame);
	return *m_pGame;
}


Script::ScriptHost &Session::Scripts()
{
	JETTISON_ASSERT(m_pScripts);
	return *m_pScripts;
}
} // namespace Jettison::Host
