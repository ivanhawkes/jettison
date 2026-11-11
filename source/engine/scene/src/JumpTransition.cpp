#include <jettison/scene/JumpTransition.h>

#include <algorithm>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <jettison/core/Log.h>
#include <jettison/resource/ResourceManager.h>

namespace Jettison::Scene
{
namespace
{
constexpr Core::Category kJump{"jump"};

// The share of a fade a frame of that many seconds covers; a fade of no length is over at once.
double Share(double realSeconds, double fadeSeconds)
{
	return fadeSeconds > 0.0 ? realSeconds / fadeSeconds : 1.0;
}
} // namespace

void RegisterJumpLoaders(Resource::ResourceManager &resources)
{
	resources.RegisterLoader<TextFile>(
	    {[](std::span<const std::byte> bytes, std::string_view)
	     {
		     return std::make_unique<TextFile>(
		         TextFile{std::string{reinterpret_cast<const char *>(bytes.data()), bytes.size()}});
	     },
	     {},
	     {}});
}

JumpTransition::JumpTransition(Resource::ResourceManager &resources, Show show, JumpSettings settings)
    : m_resources{resources}, m_show{std::move(show)}, m_settings{settings}
{
}

JumpTransition::~JumpTransition()
{
	ReleaseFiles();
}

bool JumpTransition::Begin(
    const Celestial::Galaxy &galaxy, std::size_t destination, std::string arrival)
{
	if (Active())
		return false;
	const Celestial::GalaxySystem &entry = galaxy.systems.at(destination);
	m_pGalaxy = &galaxy;
	m_destination = destination;
	m_arrivalPoint = std::move(arrival);
	m_error.clear();
	m_seconds = 0.0;
	m_streamingSeconds = 0.0;
	m_arrival.reset();
	m_systemFile = m_resources.Load<TextFile>(entry.system);
	if (entry.scene)
		m_sceneFile = m_resources.Load<TextFile>(*entry.scene);
	m_stage = JumpStage::FadingOut;
	Core::Log(
	    Core::Severity::Info, kJump, "Jump to '{}'{}", entry.name,
	    m_arrivalPoint.empty() ? std::string{} : " at '" + m_arrivalPoint + "'");
	return true;
}

void JumpTransition::Advance(double realSeconds)
{
	if (m_stage == JumpStage::Idle)
		return;
	m_seconds += realSeconds;

	if (m_stage == JumpStage::FadingOut || m_stage == JumpStage::Reading)
	{
		m_cover = std::min(1.0, m_cover + Share(realSeconds, m_settings.fadeOutSeconds));
		const auto read = [this](Resource::ResourceHandle<TextFile> file)
		{ return file.IsNull() || m_resources.StatusOf(file) != Resource::Status::Loading; };
		if (!m_arrival && read(m_systemFile) && read(m_sceneFile))
			Read();
		if (m_stage == JumpStage::FadingIn || m_cover < 1.0)
			return;
		if (!m_arrival)
		{
			m_stage = JumpStage::Reading;
			return;
		}
		try
		{
			m_show(*m_arrival);
		}
		catch (...)
		{
			m_arrival.reset();
			m_stage = JumpStage::FadingIn;
			throw;
		}
		m_arrival.reset();
		m_stage = JumpStage::Streaming;
		m_framesShown = 0;
		return;
	}

	if (m_stage == JumpStage::Streaming)
	{
		// The frame show was called in has been drawn by now, so whatever it asked for is loading.
		++m_framesShown;
		m_streamingSeconds += realSeconds;
		const bool streamed = m_resources.Loading() == 0;
		if (streamed || m_streamingSeconds >= m_settings.streamTimeout)
		{
			if (!streamed)
				Core::Log(
				    Core::Severity::Warning, kJump,
				    "'{}' still has {} resources loading after {:.1f} s; showing it as they arrive",
				    m_pGalaxy->systems[m_destination].name, m_resources.Loading(), m_streamingSeconds);
			m_stage = JumpStage::FadingIn;
		}
		return;
	}

	m_cover = std::max(0.0, m_cover - Share(realSeconds, m_settings.fadeInSeconds));
	if (m_cover == 0.0)
	{
		m_stage = JumpStage::Idle;
		if (m_error.empty())
			Core::Log(
			    Core::Severity::Info, kJump, "Arrived in '{}' after {:.2f} s, {:.2f} s of it streaming",
			    m_pGalaxy->systems[m_destination].name, m_seconds, m_streamingSeconds);
	}
}

float JumpTransition::Fade() const
{
	const double cover = std::clamp(m_cover, 0.0, 1.0);
	return static_cast<float>(cover * cover * (3.0 - 2.0 * cover));
}

void JumpTransition::Read()
{
	const Celestial::GalaxySystem &entry = m_pGalaxy->systems[m_destination];
	const auto textOf = [this](Resource::ResourceHandle<TextFile> file)
	{
		if (const TextFile *read = m_resources.TryGet(file))
			return std::string_view{read->text};
		throw std::runtime_error("it could not be read: " + std::string{m_resources.ErrorOf(file)});
	};
	try
	{
		JumpArrival arrival;
		arrival.system = m_destination;
		arrival.starSystemPath = entry.system;
		arrival.arrival = m_arrivalPoint;
		const std::string_view systemText = textOf(m_systemFile);
		try
		{
			arrival.starSystem = Celestial::ReadStarSystem(systemText);
		}
		catch (const std::exception &error)
		{
			throw std::runtime_error("'" + entry.system + "': " + error.what());
		}
		Celestial::CheckJumpPoints(*m_pGalaxy, m_destination, arrival.starSystem);
		if (!m_arrivalPoint.empty())
		{
			const int body = arrival.starSystem.Find(m_arrivalPoint);
			if (body < 0 || arrival.starSystem.bodies[static_cast<std::size_t>(body)].kind !=
			                    Celestial::BodyKind::JumpPoint)
				throw std::runtime_error("it has no jump point '" + m_arrivalPoint + "' to arrive at");
		}
		if (entry.scene)
		{
			const std::string_view sceneText = textOf(m_sceneFile);
			try
			{
				arrival.sceneFile.emplace(SceneFile::Read(sceneText));
			}
			catch (const std::exception &error)
			{
				throw std::runtime_error("'" + *entry.scene + "': " + error.what());
			}
			if (arrival.sceneFile->StarSystem() != entry.system)
				throw std::runtime_error(
				    "its scene '" + *entry.scene + "' names the system file '" +
				    arrival.sceneFile->StarSystem() + "', not '" + entry.system + "'");
			for (const std::string &body : arrival.sceneFile->CelestialBodies())
				if (arrival.starSystem.Find(body) < 0)
					throw std::runtime_error(
					    "its scene '" + *entry.scene + "' presents '" + body +
					    "', which the system does not have");
		}
		m_arrival.emplace(std::move(arrival));
		ReleaseFiles();
	}
	catch (const std::exception &error)
	{
		Abandon("Jump to '" + entry.name + "' abandoned: " + error.what());
	}
}

void JumpTransition::Abandon(std::string error)
{
	Core::Log(Core::Severity::Error, kJump, "{}", error);
	m_error = std::move(error);
	ReleaseFiles();
	m_arrival.reset();
	m_stage = JumpStage::FadingIn;
}

void JumpTransition::ReleaseFiles()
{
	for (Resource::ResourceHandle<TextFile> *file : {&m_systemFile, &m_sceneFile})
		if (!file->IsNull())
		{
			m_resources.Release(*file);
			*file = {};
		}
}
} // namespace Jettison::Scene
