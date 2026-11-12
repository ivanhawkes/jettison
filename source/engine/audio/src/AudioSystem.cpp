#include <jettison/audio/AudioSystem.h>

#include <jettison/core/Log.h>
#include <jettison/vfs/FileSystem.h>

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace Jettison::Audio
{
namespace
{
constexpr Core::Category kAudio{"audio"};

// Frames mixed between two passes over the resource manager's jobs when there is no device, and so
// no job thread: well under a page of a streamed sound (miniaudio's are a second), so the next page
// is always decoded before the mix reaches it.
constexpr std::size_t kMixChunkFrames = 1024;
constexpr std::uint32_t kChannels = 2;

std::size_t BusIndex(Bus bus)
{
	return static_cast<std::size_t>(bus);
}

// A file the VFS read whole. The VFS has no streaming read, and a sound file is small beside what
// decoding it costs: a streamed piece of music is read once, as compressed bytes, and decoded a page
// at a time from memory.
struct OpenFile
{
	std::vector<std::byte> bytes;
	std::size_t cursor{0};
};

// miniaudio's VFS is a pointer to a struct whose first member is its callbacks.
struct VfsBridge
{
	ma_vfs_callbacks callbacks{};
	const Vfs::FileSystem *pFiles{nullptr};
};

OpenFile &FileOf(ma_vfs_file file)
{
	return *static_cast<OpenFile *>(file);
}

ma_result OnOpen(ma_vfs *pVfs, const char *pPath, ma_uint32 openMode, ma_vfs_file *pFile)
{
	if (pFile == nullptr || (openMode & MA_OPEN_MODE_WRITE) != 0)
		return MA_INVALID_ARGS;
	const auto &bridge = *static_cast<VfsBridge *>(pVfs);
	if (!Vfs::FileSystem::IsValidPath(pPath))
		return MA_INVALID_FILE;
	std::optional<std::vector<std::byte>> bytes = bridge.pFiles->Read(pPath);
	if (!bytes)
		return MA_DOES_NOT_EXIST;
	// miniaudio holds the file by an opaque pointer until it closes it, which takes it back.
	*pFile = std::make_unique<OpenFile>(OpenFile{std::move(*bytes), 0}).release();
	return MA_SUCCESS;
}

ma_result OnOpenWide(
    ma_vfs * /*unused*/, const wchar_t * /*unused*/, ma_uint32 /*unused*/, ma_vfs_file * /*unused*/)
{
	return MA_NOT_IMPLEMENTED;
}

ma_result OnClose(ma_vfs * /*unused*/, ma_vfs_file file)
{
	std::unique_ptr<OpenFile>{static_cast<OpenFile *>(file)}.reset();
	return MA_SUCCESS;
}

ma_result OnRead(ma_vfs * /*unused*/, ma_vfs_file file, void *pDestination, size_t size, size_t *pRead)
{
	OpenFile &open = FileOf(file);
	const std::size_t count = std::min(size, open.bytes.size() - open.cursor);
	if (count > 0)
		std::memcpy(pDestination, open.bytes.data() + open.cursor, count);
	open.cursor += count;
	if (pRead != nullptr)
		*pRead = count;
	return count == 0 && size > 0 ? MA_AT_END : MA_SUCCESS;
}

ma_result OnWrite(
    ma_vfs * /*unused*/, ma_vfs_file /*unused*/, const void * /*unused*/, size_t /*unused*/,
    size_t * /*unused*/)
{
	return MA_NOT_IMPLEMENTED;
}

ma_result OnSeek(ma_vfs * /*unused*/, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
	OpenFile &open = FileOf(file);
	const auto size = static_cast<ma_int64>(open.bytes.size());
	const ma_int64 base = origin == ma_seek_origin_start     ? 0
	                      : origin == ma_seek_origin_current ? static_cast<ma_int64>(open.cursor)
	                                                         : size;
	const ma_int64 target = base + offset;
	if (target < 0 || target > size)
		return MA_BAD_SEEK;
	open.cursor = static_cast<std::size_t>(target);
	return MA_SUCCESS;
}

ma_result OnTell(ma_vfs * /*unused*/, ma_vfs_file file, ma_int64 *pCursor)
{
	*pCursor = static_cast<ma_int64>(FileOf(file).cursor);
	return MA_SUCCESS;
}

ma_result OnInfo(ma_vfs * /*unused*/, ma_vfs_file file, ma_file_info *pInfo)
{
	pInfo->sizeInBytes = FileOf(file).bytes.size();
	return MA_SUCCESS;
}

void Check(ma_result result, std::string_view what)
{
	if (result != MA_SUCCESS)
		throw std::runtime_error(
		    std::string("Audio: ") + std::string(what) + ": " + ma_result_description(result));
}
} // namespace

namespace Detail
{
struct Source
{
	// miniaudio keeps pointers into a sound, so each has an address of its own.
	std::unique_ptr<ma_sound> pSound;
	std::uint32_t generation{0};
	bool positioned{false};
	glm::dvec3 position{0.0};
};

class AudioState
{
  public:
	AudioState(const Vfs::FileSystem &files, const AudioOptions &options)
	{
		m_vfs.pFiles = &files;
		m_vfs.callbacks = {OnOpen, OnOpenWide, OnClose, OnRead, OnWrite, OnSeek, OnTell, OnInfo};

		Check(ma_log_init(nullptr, &m_log), "the log");
		Check(ma_log_register_callback(&m_log, ma_log_callback_init(OnLog, this)), "the log");

		// Decoded to the mixer's own format and rate, once, rather than converted as it mixes.
		ma_resource_manager_config resources = ma_resource_manager_config_init();
		resources.pLog = &m_log;
		resources.pVFS = &m_vfs;
		resources.decodedFormat = ma_format_f32;
		resources.decodedChannels = 0;
		resources.decodedSampleRate = options.sampleRate;
		// With no device, nothing mixes until Mix asks, so the jobs that decode a streamed sound's
		// next page run there, on the caller's thread, and the output is the same every run.
		const bool device = options.output != Output::None;
		if (!device)
		{
			resources.jobThreadCount = 0;
			resources.flags = MA_RESOURCE_MANAGER_FLAG_NO_THREADING;
		}
		Check(ma_resource_manager_init(&resources, &m_resources), "the resource manager");
		m_resourcesMade = true;

		ma_engine_config engine = ma_engine_config_init();
		engine.pResourceManager = &m_resources;
		engine.pLog = &m_log;
		engine.channels = kChannels;
		engine.sampleRate = options.sampleRate;
		engine.listenerCount = 1;
		engine.noDevice = device ? MA_FALSE : MA_TRUE;
		const ma_backend null = ma_backend_null;
		if (options.output == Output::Null)
		{
			Check(ma_context_init(&null, 1, nullptr, &m_context), "the null backend");
			m_contextMade = true;
			engine.pContext = &m_context;
		}
		ma_result result = ma_engine_init(&engine, &m_engine);
		if (result != MA_SUCCESS && options.output == Output::Device)
		{
			// A context whose backend loaded can still find no device to open (a session with no
			// sound server, a runner with no sound card), and miniaudio does not try the next
			// backend then. The null backend plays to nowhere, at the device's pace.
			Core::Log(
			    Core::Severity::Warning, kAudio, "No playback device ({}); playing to none",
			    ma_result_description(result));
			Check(ma_context_init(&null, 1, nullptr, &m_context), "the null backend");
			m_contextMade = true;
			engine.pContext = &m_context;
			result = ma_engine_init(&engine, &m_engine);
		}
		Check(result, "the mixer");
		m_engineMade = true;
		m_device = device;
		m_sampleRate = ma_engine_get_sample_rate(&m_engine);

		for (const Bus bus : kBuses)
		{
			ma_sound_group *pParent = bus == Bus::Master ? nullptr : &m_buses[BusIndex(Bus::Master)];
			Check(ma_sound_group_init(&m_engine, 0, pParent, &m_buses[BusIndex(bus)]), "a bus");
			++m_busesMade;
		}
		if (m_device)
			Core::Log(
			    Core::Severity::Info, kAudio, "Playing through {} at {} Hz",
			    ma_get_backend_name(ma_engine_get_device(&m_engine)->pContext->backend), m_sampleRate);
		// Slot 0 is the empty handle's.
		m_sources.emplace_back();
	}

	~AudioState()
	{
		for (Source &source : m_sources)
			if (source.pSound)
				ma_sound_uninit(source.pSound.get());
		for (std::size_t bus = m_busesMade; bus-- > 0;)
			ma_sound_group_uninit(&m_buses[bus]);
		if (m_engineMade)
			ma_engine_uninit(&m_engine);
		if (m_contextMade)
			ma_context_uninit(&m_context);
		if (m_resourcesMade)
			ma_resource_manager_uninit(&m_resources);
		ma_log_uninit(&m_log);
	}
	AudioState(const AudioState &) = delete;
	AudioState &operator=(const AudioState &) = delete;

	SourceHandle Start(std::string_view virtualPath, Bus bus, ma_uint32 flags, bool loop)
	{
		const std::string path(virtualPath);
		auto pSound = std::make_unique<ma_sound>();
		const ma_result result = ma_sound_init_from_file(
		    &m_engine, path.c_str(), flags, &m_buses[BusIndex(bus)], nullptr, pSound.get());
		if (result != MA_SUCCESS)
		{
			++m_counters.failed;
			Core::Log(
			    Core::Severity::Warning, kAudio, "Cannot play {}: {}", virtualPath,
			    ma_result_description(result));
			return {};
		}
		ma_sound_set_looping(pSound.get(), loop ? MA_TRUE : MA_FALSE);
		// Nothing reports velocities, and a Doppler shift from positions alone would follow the
		// floating origin's jumps.
		ma_sound_set_doppler_factor(pSound.get(), 0.0f);

		std::uint32_t index = 0;
		if (!m_free.empty())
		{
			index = m_free.back();
			m_free.pop_back();
		}
		else
		{
			index = static_cast<std::uint32_t>(m_sources.size());
			m_sources.emplace_back();
		}
		Source &source = m_sources[index];
		source.pSound = std::move(pSound);
		source.positioned = false;
		++m_counters.started[BusIndex(bus)];
		return {index, source.generation};
	}

	Source *Find(SourceHandle handle)
	{
		if (handle.index == 0 || handle.index >= m_sources.size())
			return nullptr;
		Source &source = m_sources[handle.index];
		return source.pSound && source.generation == handle.generation ? &source : nullptr;
	}
	const Source *Find(SourceHandle handle) const
	{
		return const_cast<AudioState *>(this)->Find(handle);
	}

	void Release(std::uint32_t index)
	{
		Source &source = m_sources[index];
		ma_sound_uninit(source.pSound.get());
		source.pSound.reset();
		++source.generation;
		m_free.push_back(index);
	}

	void Place(Source &source)
	{
		const glm::vec3 relative(source.position - m_listenerPosition);
		ma_sound_set_position(source.pSound.get(), relative.x, relative.y, relative.z);
	}

	static void OnLog(void *pUserData, ma_uint32 level, const char *pMessage)
	{
		auto &state = *static_cast<AudioState *>(pUserData);
		std::string_view message(pMessage);
		while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
			message.remove_suffix(1);
		// miniaudio's info is a device's details, line by line: debug here, with one line of the
		// engine's own at info saying what plays through what.
		Core::Severity severity = Core::Severity::Debug;
		if (level == MA_LOG_LEVEL_ERROR)
			severity = Core::Severity::Error;
		else if (level == MA_LOG_LEVEL_WARNING)
			severity = Core::Severity::Warning;
		if (severity >= Core::Severity::Warning)
			state.m_problems.fetch_add(1, std::memory_order_relaxed);
		Core::Log(severity, kAudio, "{}", message);
	}

	VfsBridge m_vfs;
	ma_log m_log{};
	ma_resource_manager m_resources{};
	ma_context m_context{};
	ma_engine m_engine{};
	std::array<ma_sound_group, kBuses.size()> m_buses{};
	bool m_resourcesMade{false};
	bool m_contextMade{false};
	bool m_engineMade{false};
	std::size_t m_busesMade{0};
	bool m_device{false};
	std::uint32_t m_sampleRate{0};

	std::vector<Source> m_sources;
	std::vector<std::uint32_t> m_free;
	SourceHandle m_music;
	glm::dvec3 m_listenerPosition{0.0};
	AudioCounters m_counters;
	// miniaudio logs from its own threads too.
	std::atomic<std::size_t> m_problems{0};
};
} // namespace Detail

std::string_view BusName(Bus bus)
{
	switch (bus)
	{
		case Bus::Master: return "master";
		case Bus::Music: return "music";
		case Bus::Effects: return "effects";
		case Bus::Ui: return "ui";
		case Bus::Voice: return "voice";
	}
	return "master";
}

std::optional<Bus> BusFromName(std::string_view name)
{
	for (const Bus bus : kBuses)
		if (BusName(bus) == name)
			return bus;
	return std::nullopt;
}

AudioSystem::AudioSystem(const Vfs::FileSystem &files, const AudioOptions &options)
    : m_pState(std::make_unique<Detail::AudioState>(files, options))
{
}

AudioSystem::~AudioSystem() = default;

void AudioSystem::SetBusVolume(Bus bus, float volume)
{
	ma_sound_group_set_volume(&m_pState->m_buses[BusIndex(bus)], std::clamp(volume, 0.0f, 4.0f));
}

float AudioSystem::BusVolume(Bus bus) const
{
	return ma_sound_group_get_volume(&m_pState->m_buses[BusIndex(bus)]);
}

SourceHandle AudioSystem::Play(
    std::string_view virtualPath, Bus bus, const glm::dvec3 &position, bool loop,
    const Falloff &falloff)
{
	const SourceHandle handle = m_pState->Start(virtualPath, bus, MA_SOUND_FLAG_DECODE, loop);
	Detail::Source *pSource = m_pState->Find(handle);
	if (pSource == nullptr)
		return handle;
	ma_sound *pSound = pSource->pSound.get();
	ma_sound_set_positioning(pSound, ma_positioning_absolute);
	ma_sound_set_attenuation_model(pSound, ma_attenuation_model_inverse);
	ma_sound_set_min_distance(pSound, falloff.minDistance);
	ma_sound_set_max_distance(pSound, falloff.maxDistance);
	ma_sound_set_rolloff(pSound, falloff.rolloff);
	pSource->positioned = true;
	pSource->position = position;
	// Placed before it starts, so its first frames are mixed where it is.
	m_pState->Place(*pSource);
	ma_sound_start(pSound);
	return handle;
}

SourceHandle AudioSystem::PlayFlat(std::string_view virtualPath, Bus bus, bool loop)
{
	const SourceHandle handle =
	    m_pState->Start(virtualPath, bus, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, loop);
	if (Detail::Source *pSource = m_pState->Find(handle))
		ma_sound_start(pSource->pSound.get());
	return handle;
}

SourceHandle AudioSystem::PlayMusic(std::string_view virtualPath, bool loop)
{
	StopMusic();
	m_pState->m_music = m_pState->Start(
	    virtualPath, Bus::Music, MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, loop);
	if (Detail::Source *pSource = m_pState->Find(m_pState->m_music))
		ma_sound_start(pSource->pSound.get());
	return m_pState->m_music;
}

void AudioSystem::StopMusic()
{
	Stop(m_pState->m_music);
	m_pState->m_music = {};
}

void AudioSystem::Stop(SourceHandle source)
{
	if (m_pState->Find(source) != nullptr)
		m_pState->Release(source.index);
}

void AudioSystem::SetPosition(SourceHandle source, const glm::dvec3 &position)
{
	if (Detail::Source *pSource = m_pState->Find(source); pSource && pSource->positioned)
		pSource->position = position;
}

void AudioSystem::SetVolume(SourceHandle source, float volume)
{
	if (Detail::Source *pSource = m_pState->Find(source))
		ma_sound_set_volume(pSource->pSound.get(), std::max(volume, 0.0f));
}

bool AudioSystem::IsPlaying(SourceHandle source) const
{
	return m_pState->Find(source) != nullptr;
}

std::size_t AudioSystem::Playing() const
{
	return static_cast<std::size_t>(std::ranges::count_if(
	    m_pState->m_sources, [](const Detail::Source &s) { return s.pSound != nullptr; }));
}

void AudioSystem::SetListener(const glm::dvec3 &position, const glm::dquat &orientation)
{
	m_pState->m_listenerPosition = position;
	// The listener stays at the origin; only its orientation is miniaudio's.
	const glm::vec3 forward(orientation * glm::dvec3(0.0, 0.0, -1.0));
	const glm::vec3 up(orientation * glm::dvec3(0.0, 1.0, 0.0));
	ma_engine *pEngine = &m_pState->m_engine;
	ma_engine_listener_set_position(pEngine, 0, 0.0f, 0.0f, 0.0f);
	ma_engine_listener_set_direction(pEngine, 0, forward.x, forward.y, forward.z);
	ma_engine_listener_set_world_up(pEngine, 0, up.x, up.y, up.z);
}

void AudioSystem::Update()
{
	std::vector<Detail::Source> &sources = m_pState->m_sources;
	for (std::uint32_t index = 1; index < sources.size(); ++index)
	{
		Detail::Source &source = sources[index];
		if (!source.pSound)
			continue;
		// A looping sound is never at its end; one that ended is let go, so its handle names nothing.
		if (ma_sound_at_end(source.pSound.get()) != MA_FALSE)
		{
			m_pState->Release(index);
			continue;
		}
		if (source.positioned)
			m_pState->Place(source);
	}
}

std::size_t AudioSystem::Mix(std::span<float> interleaved)
{
	if (m_pState->m_device)
		throw std::logic_error("Audio: Mix is for a system with no device; the device mixes its own");
	const std::size_t frames = interleaved.size() / kChannels;
	std::size_t written = 0;
	while (written < frames)
	{
		while (ma_resource_manager_process_next_job(&m_pState->m_resources) == MA_SUCCESS) {}
		const std::size_t chunk = std::min(kMixChunkFrames, frames - written);
		ma_uint64 read = 0;
		ma_engine_read_pcm_frames(
		    &m_pState->m_engine, interleaved.data() + written * kChannels, chunk, &read);
		// The engine mixes silence when nothing plays, so it always fills what it was asked for.
		written += static_cast<std::size_t>(read);
		if (read == 0)
			break;
	}
	return written;
}

bool AudioSystem::HasDevice() const
{
	return m_pState->m_device;
}

std::uint32_t AudioSystem::SampleRate() const
{
	return m_pState->m_sampleRate;
}

const AudioCounters &AudioSystem::Counters() const
{
	return m_pState->m_counters;
}

std::size_t AudioSystem::Problems() const
{
	return m_pState->m_problems.load(std::memory_order_relaxed) + m_pState->m_counters.failed;
}
} // namespace Jettison::Audio
