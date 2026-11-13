#include <jettison/audio/AudioSystem.h>
#include <jettison/audio/EventSounds.h>
#include <jettison/audio/SoundSources.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/scene/Components.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Events.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;
using Audio::AudioSystem;
using Audio::Bus;

namespace
{
// The shipped sounds, mounted on their own: the definitions under the assets' defs/sounds name the
// shell's events, which these tests do not register.
constexpr const char *kFilled = "ui/trade-filled.wav";
constexpr const char *kDocked = "effects/docked.wav";
constexpr const char *kMusic = "music/drift.flac";

// The shipped sounds, and a scratch mount over them for definitions a test writes.
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-audio-tests-" + std::to_string(getpid()))}
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory);
		m_files.AddMount({"sounds", std::filesystem::path(JETTISON_ASSET_SOURCE_DIR) / "audio", false});
		m_files.AddMount({"scratch", m_directory, false});
	}
	~Scratch()
	{
		std::error_code error;
		std::filesystem::remove_all(m_directory, error);
	}
	Scratch(const Scratch &) = delete;
	Scratch &operator=(const Scratch &) = delete;

	void Put(const std::string &path, const std::string &contents) const
	{
		const auto file = m_directory / path;
		std::filesystem::create_directories(file.parent_path());
		std::ofstream{file, std::ios::binary | std::ios::trunc} << contents;
	}
	const Vfs::FileSystem &Files() const
	{
		return m_files;
	}

  private:
	std::filesystem::path m_directory;
	Vfs::FileSystem m_files;
};

Audio::AudioOptions NoDevice()
{
	return {.output = Audio::Output::None};
}

// The loudest sample in each channel over that many seconds of output.
struct Peaks
{
	float left{0.0f};
	float right{0.0f};
	float Both() const
	{
		return std::max(left, right);
	}
};
Peaks MixPeaks(AudioSystem &audio, double seconds)
{
	std::vector<float> buffer(std::size_t{2} * 4800);
	Peaks peaks;
	auto frames = static_cast<std::size_t>(seconds * audio.SampleRate());
	while (frames > 0)
	{
		const std::size_t chunk = std::min(frames, buffer.size() / 2);
		const std::size_t mixed = audio.Mix({buffer.data(), chunk * 2});
		REQUIRE(mixed == chunk);
		for (std::size_t i = 0; i < chunk; ++i)
		{
			peaks.left = std::max(peaks.left, std::abs(buffer[2 * i]));
			peaks.right = std::max(peaks.right, std::abs(buffer[2 * i + 1]));
		}
		frames -= chunk;
		audio.Update();
	}
	return peaks;
}

// A ship and a station, as an event names them.
struct TestDocked
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	int count{0};
};
struct TestUnheard
{
	int count{0};
};

void RegisterTestTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<TestDocked>("AudioTests.Docked")
		    .Field<&TestDocked::ship>("ship")
		    .Field<&TestDocked::station>("station")
		    .Field<&TestDocked::count>("count");
		RegisterType<TestUnheard>("AudioTests.Unheard").Field<&TestUnheard::count>("count");
		return true;
	}();
	(void)registered;
	Audio::RegisterTypes();
}

Audio::SoundComponent SoundOf(const char *sound)
{
	Audio::SoundComponent component;
	component.sound = sound;
	return component;
}

Sim::SimEvent EventOf(auto event)
{
	return {1, entt::meta_any{std::move(event)}};
}
} // namespace

TEST_CASE("Audio: the buses are named as settings and the console name them")
{
	for (const Bus bus : Audio::kBuses)
		CHECK(Audio::BusFromName(Audio::BusName(bus)) == bus);
	CHECK(Audio::BusName(Bus::Effects) == "effects");
	CHECK_FALSE(Audio::BusFromName("sfx"));
}

TEST_CASE("Audio: a sound plays on its bus until it ends, and its handle then names nothing")
{
	Scratch scratch;
	AudioSystem audio(scratch.Files(), NoDevice());
	CHECK_FALSE(audio.HasDevice());
	CHECK(audio.SampleRate() == 48000);

	const Audio::SourceHandle sound = audio.PlayFlat(kFilled, Bus::Ui);
	REQUIRE(sound != Audio::SourceHandle{});
	CHECK(audio.IsPlaying(sound));
	CHECK(audio.Counters().started[static_cast<std::size_t>(Bus::Ui)] == 1);

	// The sound is 0.45 s long and normalised to a peak of 0.7.
	const Peaks heard = MixPeaks(audio, 0.5);
	CHECK(heard.Both() > 0.3f);
	CHECK(heard.Both() < 1.0f);
	CHECK_FALSE(audio.IsPlaying(sound));
	CHECK(audio.Playing() == 0);
	CHECK(MixPeaks(audio, 0.1).Both() == 0.0f);

	// The slot is used again, and the old handle does not name the new sound.
	const Audio::SourceHandle again = audio.PlayFlat(kFilled, Bus::Ui);
	CHECK(again.index == sound.index);
	CHECK(again != sound);
	audio.Stop(sound);
	CHECK(audio.IsPlaying(again));
	audio.Stop(again);
	CHECK_FALSE(audio.IsPlaying(again));
	CHECK(audio.Problems() == 0);
}

TEST_CASE("Audio: a bus's volume scales what plays on it, and master's everything")
{
	Scratch scratch;
	AudioSystem audio(scratch.Files(), NoDevice());
	audio.PlayFlat(kFilled, Bus::Ui);
	const float full = MixPeaks(audio, 0.5).Both();

	audio.SetBusVolume(Bus::Ui, 0.5f);
	CHECK(audio.BusVolume(Bus::Ui) == doctest::Approx(0.5f));
	audio.PlayFlat(kFilled, Bus::Ui);
	CHECK(MixPeaks(audio, 0.5).Both() == doctest::Approx(full * 0.5f).epsilon(0.02));

	// The effects bus is not the UI's.
	audio.PlayFlat(kFilled, Bus::Effects);
	CHECK(MixPeaks(audio, 0.5).Both() == doctest::Approx(full).epsilon(0.02));

	audio.SetBusVolume(Bus::Master, 0.0f);
	MixPeaks(audio, 0.1);
	audio.PlayFlat(kFilled, Bus::Effects);
	CHECK(MixPeaks(audio, 0.5).Both() == 0.0f);

	audio.SetBusVolume(Bus::Master, 9.0f);
	CHECK(audio.BusVolume(Bus::Master) == 4.0f);
}

TEST_CASE("Audio: a positioned sound is heard from its side, and quieter with distance")
{
	Scratch scratch;
	AudioSystem audio(scratch.Files(), NoDevice());
	audio.SetListener({0.0, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0});
	const Audio::Falloff falloff{.minDistance = 10.0f, .maxDistance = 100000.0f, .rolloff = 1.0f};

	// The listener looks down -Z with +Y up, so +X is to its right.
	audio.Play(kDocked, Bus::Effects, {10.0, 0.0, 0.0}, false, falloff);
	const Peaks right = MixPeaks(audio, 1.2);
	CHECK(right.right > 2.0f * right.left);

	audio.Play(kDocked, Bus::Effects, {-10.0, 0.0, 0.0}, false, falloff);
	const Peaks left = MixPeaks(audio, 1.2);
	CHECK(left.left > 2.0f * left.right);

	// Ten times the minimum distance, a tenth of the gain: minDistance / (minDistance + 1 * 90).
	audio.Play(kDocked, Bus::Effects, {0.0, 0.0, -100.0}, false, falloff);
	const float far = MixPeaks(audio, 1.2).Both();
	audio.Play(kDocked, Bus::Effects, {0.0, 0.0, -10.0}, false, falloff);
	const float near = MixPeaks(audio, 1.2).Both();
	CHECK(far == doctest::Approx(near * 0.1f).epsilon(0.05));

	// Turning the listener about +Y by a quarter turn to the left puts the +X sound behind it,
	// and the -Z one on its right.
	const glm::dquat turned = glm::angleAxis(glm::half_pi<double>(), glm::dvec3{0.0, 1.0, 0.0});
	audio.SetListener({0.0, 0.0, 0.0}, turned);
	audio.Play(kDocked, Bus::Effects, {0.0, 0.0, -10.0}, false, falloff);
	const Peaks turnedRight = MixPeaks(audio, 1.2);
	CHECK(turnedRight.right > 2.0f * turnedRight.left);
	CHECK(audio.Problems() == 0);
}

TEST_CASE("Audio: a sound is placed relative to the listener, as precisely far from the origin")
{
	Scratch scratch;
	const Audio::Falloff falloff{.minDistance = 10.0f, .maxDistance = 100000.0f, .rolloff = 1.0f};
	const auto heard = [&](const glm::dvec3 &listener)
	{
		AudioSystem audio(scratch.Files(), NoDevice());
		audio.SetListener(listener, glm::dquat{1.0, 0.0, 0.0, 0.0});
		audio.Play(kDocked, Bus::Effects, listener + glm::dvec3{30.0, 0.0, -40.0}, false, falloff);
		return MixPeaks(audio, 1.2);
	};
	// A hundred million kilometres out, where a float has a step of eight kilometres.
	const Peaks home = heard({0.0, 0.0, 0.0});
	const Peaks out = heard({1.0e11, 0.0, -1.0e11});
	CHECK(out.left == home.left);
	CHECK(out.right == home.right);
}

TEST_CASE("Audio: a source follows the position it is given")
{
	Scratch scratch;
	AudioSystem audio(scratch.Files(), NoDevice());
	audio.SetListener({0.0, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0});
	const Audio::SourceHandle source = audio.Play(
	    kDocked, Bus::Effects, {10.0, 0.0, 0.0}, true, {.minDistance = 10.0f, .maxDistance = 1.0e5f});
	MixPeaks(audio, 0.05);
	audio.SetPosition(source, {-10.0, 0.0, 0.0});
	audio.Update();
	// miniaudio pans to the new place over a few milliseconds rather than with a click; the loop's
	// next start, 1.1 s in, is heard from there.
	MixPeaks(audio, 0.2);
	const Peaks moved = MixPeaks(audio, 1.2);
	CHECK(moved.left > 2.0f * moved.right);
	// A looping sound plays past its length.
	CHECK(audio.IsPlaying(source));
}

TEST_CASE("Audio: music streams, loops, and a second piece replaces the first")
{
	Scratch scratch;
	AudioSystem audio(scratch.Files(), NoDevice());
	const Audio::SourceHandle music = audio.PlayMusic(kMusic);
	REQUIRE(music != Audio::SourceHandle{});
	CHECK(audio.Counters().started[static_cast<std::size_t>(Bus::Music)] == 1);
	CHECK(MixPeaks(audio, 2.0).Both() > 0.05f);

	// The piece is 32 s long; it is still playing, and heard, past its end.
	MixPeaks(audio, 31.0);
	CHECK(audio.IsPlaying(music));
	CHECK(MixPeaks(audio, 2.0).Both() > 0.05f);

	// A change of volume is eased in over a few milliseconds, so it does not click.
	audio.SetBusVolume(Bus::Music, 0.0f);
	MixPeaks(audio, 0.1);
	CHECK(MixPeaks(audio, 0.5).Both() == 0.0f);
	audio.SetBusVolume(Bus::Music, 1.0f);

	const Audio::SourceHandle second = audio.PlayMusic(kMusic, false);
	CHECK_FALSE(audio.IsPlaying(music));
	CHECK(audio.IsPlaying(second));
	CHECK(audio.Playing() == 1);
	audio.StopMusic();
	CHECK(audio.Playing() == 0);
	CHECK(audio.Problems() == 0);
}

TEST_CASE("Audio: a sound that cannot be played is reported and counted, not thrown")
{
	Scratch scratch;
	scratch.Put("broken.wav", "RIFF but not really");
	AudioSystem audio(scratch.Files(), NoDevice());
	CHECK(audio.PlayFlat("missing.wav", Bus::Ui) == Audio::SourceHandle{});
	CHECK(audio.Play("broken.wav", Bus::Effects, {0.0, 0.0, 0.0}) == Audio::SourceHandle{});
	CHECK(audio.PlayFlat("../outside.wav", Bus::Ui) == Audio::SourceHandle{});
	CHECK(audio.Counters().failed == 3);
	CHECK(audio.Problems() >= 3);
	CHECK(audio.Playing() == 0);
	// A handle to nothing is ignored.
	audio.SetVolume({}, 1.0f);
	audio.Stop({});
}

TEST_CASE("Audio: Mix is refused with a device, which mixes on its own")
{
	Scratch scratch;
	// The null backend is a device that plays to nowhere, at the pace a sound card would.
	AudioSystem audio(scratch.Files(), {.output = Audio::Output::Null});
	CHECK(audio.HasDevice());
	std::vector<float> buffer(64);
	CHECK_THROWS_AS(audio.Mix(buffer), std::logic_error);
}

TEST_CASE("Audio: an event plays the sounds its definitions map it to, placed at the entity named")
{
	RegisterTestTypes();
	Scratch scratch;
	scratch.Put(
	    "defs/sounds/docked.yaml", "version: 1\nevent: AudioTests.Docked\nsound: effects/docked.wav\n"
	                               "bus: Effects\nat: station\nminDistance: 10\n");
	scratch.Put(
	    "defs/sounds/docked-chime.yaml", "version: 1\nevent: AudioTests.Docked\nsound: "
	                                     "ui/trade-filled.wav\nbus: Ui\nvolume: 0.5\n");
	Sim::DefinitionRegistry definitions(scratch.Files());
	definitions.Load<Audio::EventSound>(Audio::kEventSoundDirectory, {Audio::kEventSoundVersion});
	const Audio::EventSounds sounds(definitions);
	CHECK(sounds.Events() == 1);
	CHECK(sounds.Sounds() == 2);

	AudioSystem audio(scratch.Files(), NoDevice());
	audio.SetListener({0.0, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0});
	entt::registry world;
	const entt::entity ship = world.create();
	const entt::entity station = world.create();
	std::vector<entt::entity> asked;
	const Audio::PositionOf positionOf = [&](entt::entity entity) -> std::optional<glm::dvec3>
	{
		asked.push_back(entity);
		if (entity == station)
			return glm::dvec3{10.0, 0.0, 0.0};
		return std::nullopt;
	};

	CHECK(sounds.Play(EventOf(TestDocked{ship, station, 1}), audio, positionOf) == 2);
	CHECK(asked == std::vector{station});
	CHECK(audio.Counters().started[static_cast<std::size_t>(Bus::Effects)] == 1);
	CHECK(audio.Counters().started[static_cast<std::size_t>(Bus::Ui)] == 1);
	// The docked sound is at the station, on the listener's right; the chime is flat, in both ears.
	const Peaks heard = MixPeaks(audio, 1.2);
	CHECK(heard.right > 1.5f * heard.left);

	// An event no definition names plays nothing; one whose entity presentation cannot place plays
	// flat, in both ears alike.
	CHECK(sounds.Play(EventOf(TestUnheard{}), audio, positionOf) == 0);
	CHECK(sounds.Play(EventOf(TestDocked{ship, ship, 2}), audio, positionOf) == 2);
	const Peaks flat = MixPeaks(audio, 1.2);
	CHECK(flat.left == doctest::Approx(flat.right).epsilon(0.01));
	CHECK(audio.Problems() == 0);
}

TEST_CASE("Audio: an event sound naming what does not exist is refused, naming the definition")
{
	RegisterTestTypes();
	const auto refusal = [](const std::string &contents)
	{
		Scratch scratch;
		scratch.Put("defs/sounds/bad.yaml", contents);
		Sim::DefinitionRegistry definitions(scratch.Files());
		definitions.Load<Audio::EventSound>(Audio::kEventSoundDirectory, {Audio::kEventSoundVersion});
		try
		{
			const Audio::EventSounds sounds(definitions);
		}
		catch (const std::runtime_error &error)
		{
			return std::string(error.what());
		}
		return std::string();
	};
	const std::string event = refusal("version: 1\nevent: AudioTests.Dockd\nsound: a.wav\nbus: Ui\n");
	CHECK(event.find("defs/sounds/bad") != std::string::npos);
	CHECK(event.find("AudioTests.Dockd") != std::string::npos);

	// Not a field, and a field that holds no entity.
	CHECK(
	    refusal("version: 1\nevent: AudioTests.Docked\nsound: a.wav\nbus: Ui\nat: port\n")
	        .find("no entity field port") != std::string::npos);
	CHECK(
	    refusal("version: 1\nevent: AudioTests.Docked\nsound: a.wav\nbus: Ui\nat: count\n")
	        .find("no entity field count") != std::string::npos);

	// A clip's event and a simulation event both, neither, and a clip's placed at a field.
	CHECK(
	    refusal("version: 1\nevent: AudioTests.Docked\nclip: clamp.closed\nsound: a.wav\n")
	        .find("names one of them") != std::string::npos);
	CHECK(refusal("version: 1\nsound: a.wav\n").find("names one of them") != std::string::npos);
	CHECK(
	    refusal("version: 1\nclip: clamp.closed\nsound: a.wav\nat: station\n")
	        .find("placed at no field") != std::string::npos);
}

TEST_CASE("Audio: a clip's event plays its sounds from where its player is, or flat for nowhere")
{
	RegisterTestTypes();
	Scratch scratch;
	scratch.Put(
	    "defs/sounds/clamp.yaml",
	    "version: 1\nclip: clamp.closed\nsound: effects/docked.wav\nbus: Effects\nminDistance: 10\n");
	Sim::DefinitionRegistry definitions(scratch.Files());
	definitions.Load<Audio::EventSound>(Audio::kEventSoundDirectory, {Audio::kEventSoundVersion});
	const Audio::EventSounds sounds(definitions);
	CHECK(sounds.Events() == 1);
	CHECK(sounds.Sounds() == 1);

	AudioSystem audio(scratch.Files(), NoDevice());
	audio.SetListener({0.0, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0});
	CHECK(sounds.PlayClipEvent("clamp.closed", glm::dvec3{-10.0, 0.0, 0.0}, audio) == 1);
	const Peaks heard = MixPeaks(audio, 1.2);
	CHECK(heard.left > 1.5f * heard.right);
	CHECK(sounds.PlayClipEvent("clamp.released", glm::dvec3{-10.0, 0.0, 0.0}, audio) == 0);
	CHECK(sounds.PlayClipEvent("clamp.closed", std::nullopt, audio) == 1);
	const Peaks flat = MixPeaks(audio, 1.2);
	CHECK(flat.left == doctest::Approx(flat.right).epsilon(0.01));
	CHECK(audio.Counters().started[static_cast<std::size_t>(Bus::Effects)] == 2);
	CHECK(audio.Problems() == 0);
}

TEST_CASE("Audio: a presentation entity's sound starts, follows it, and stops when it goes")
{
	Scratch scratch;
	AudioSystem audio(scratch.Files(), NoDevice());
	audio.SetListener({0.0, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0});
	Audio::SoundSources sources;
	entt::registry registry;

	const entt::entity station = registry.create();
	registry.emplace<Audio::SoundComponent>(
	    station, Audio::SoundComponent{kDocked, Bus::Effects, 1.0f, {.minDistance = 10.0f}});
	sources.Update(registry, audio);
	// No transform yet: not placed, so not played.
	CHECK(sources.Size() == 0);

	auto &world = registry.emplace<Scene::WorldTransformComponent>(station);
	world.matrix[3] = glm::dvec4{10.0, 0.0, 0.0, 1.0};
	sources.Update(registry, audio);
	REQUIRE(sources.Size() == 1);
	CHECK(audio.Playing() == 1);
	const Peaks right = MixPeaks(audio, 0.6);
	CHECK(right.right > 2.0f * right.left);

	registry.get<Scene::WorldTransformComponent>(station).matrix[3] = glm::dvec4{-10.0, 0.0, 0.0, 1.0};
	sources.Update(registry, audio);
	audio.Update();
	const Peaks left = MixPeaks(audio, 0.6);
	CHECK(left.left > 2.0f * left.right);

	// A looping sound outlasts its length; it stops with its component.
	MixPeaks(audio, 2.0);
	CHECK(audio.Playing() == 1);
	registry.remove<Audio::SoundComponent>(station);
	sources.Update(registry, audio);
	CHECK(sources.Size() == 0);
	CHECK(audio.Playing() == 0);

	registry.emplace<Audio::SoundComponent>(station, SoundOf(kDocked));
	sources.Update(registry, audio);
	CHECK(audio.Playing() == 1);
	registry.destroy(station);
	sources.Update(registry, audio);
	CHECK(audio.Playing() == 0);

	const entt::entity other = registry.create();
	registry.emplace<Audio::SoundComponent>(other, SoundOf(kDocked));
	registry.emplace<Scene::WorldTransformComponent>(other);
	sources.Update(registry, audio);
	sources.Clear(audio);
	CHECK(sources.Size() == 0);
	CHECK(audio.Playing() == 0);
	CHECK(audio.Problems() == 0);
}
