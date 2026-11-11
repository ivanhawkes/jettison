#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

#include <jettison/celestial/Galaxy.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/resource/ResourceHandle.h>
#include <jettison/scene/Scene.h>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Scene
{
// A file's text, read on the I/O worker: what a jump reads its destination's files as. They are
// parsed on the main thread. The archives read EnTT's meta context, which a registration on the
// main thread may be inserting into at the same moment, and nothing in it is guarded against that;
// a system file and a scene file are a few kilobytes of YAML, where a model or a texture, which the
// worker does decode, is megabytes.
struct TextFile
{
	std::string text;
};

// Registers the loader a jump reads its files through. Once per resource manager, at start-up.
void RegisterJumpLoaders(Resource::ResourceManager &resources);

// Where a jump is (docs/architecture.md, "Jumps").
enum class JumpStage
{
	Idle,
	// The effect covers the frame, while the destination's files are read and checked.
	FadingOut,
	// The effect covers the frame and the files are still being read.
	Reading,
	// The destination is shown, under the effect, while what it draws streams in.
	Streaming,
	// The effect lifts, on the destination, or on the system the jump left if it was abandoned.
	FadingIn,
};

struct JumpSettings
{
	// Real seconds for the effect to cover the frame and to lift from it.
	double fadeOutSeconds{0.6};
	double fadeInSeconds{0.8};
	// The longest the effect is held over a shown destination while what it draws is still loading.
	// Past it the effect lifts on what has arrived, and the rest is drawn as it arrives, rather than
	// a jump being held for ever on a file that will not come.
	double streamTimeout{8.0};
};

// The destination, read and checked, for whoever owns the scene to show.
struct JumpArrival
{
	// The system's index in the galaxy.
	std::size_t system{0};
	Celestial::StarSystem starSystem;
	// The scene file the galaxy names for the system; without one, the scene is the system alone
	// (Scene::LoadSystem).
	std::optional<SceneFile> sceneFile;
	// The system file's virtual path, which the scene file names too.
	std::string starSystemPath;
	// The jump point the jump arrives at; empty for one that came along no lane.
	std::string arrival;
};

// A jump from the star system shown to another (docs/scope.md, "Scene: management"): the
// presentation's half, since the simulation is the whole galaxy's and keeps stepping through it.
// Presentation shows one system at a time, and a jump replaces it behind an effect that covers the
// frame, in stages, so the frame loop never stalls:
//
// 1. The effect rises, and meanwhile the destination's system file and its scene file, if the
//    galaxy names one, are read on the I/O worker.
// 2. Once both are read they are parsed and checked on the main thread: the system against the
//    galaxy's lanes (Celestial::CheckJumpPoints), the scene file against the system. A destination
//    that fails is abandoned, and the effect lifts on the system the jump left, which was never
//    touched.
// 3. With the effect covering the frame, the arrival is handed to show, which replaces the scene.
// 4. What the new scene draws (its models, materials, textures and rigs) streams in through the
//    resource manager, as it always does. The effect is held until nothing is loading for the first
//    time (ResourceManager::Loading) after a frame has been drawn of it, since the renderer asks
//    for a model when it first draws it, or until the stream timeout.
// 5. The effect lifts.
//
// It is driven once a frame with the frame's real seconds: the effect is presentation's, and takes
// the same time at any time compression. It names no device: Fade is what the shell hands the
// renderer. The galaxy and the resource manager outlive it.
class JumpTransition
{
  public:
	using Show = std::function<void(JumpArrival &arrival)>;

	JumpTransition(Resource::ResourceManager &resources, Show show, JumpSettings settings = {});
	~JumpTransition();

	JumpTransition(const JumpTransition &) = delete;
	JumpTransition &operator=(const JumpTransition &) = delete;

	// Starts a jump to the galaxy's system, arriving at the jump point (empty for none). False, and
	// nothing started, while a jump is under way.
	bool Begin(const Celestial::Galaxy &galaxy, std::size_t destination, std::string arrival = {});

	// One frame. An exception from show propagates, with the jump given up and the effect lifting on
	// whatever show left.
	void Advance(double realSeconds);

	JumpStage Stage() const
	{
		return m_stage;
	}
	bool Active() const
	{
		return m_stage != JumpStage::Idle;
	}
	// How much of the frame the effect covers, from 0 to 1, eased.
	float Fade() const;
	// Why the last jump was abandoned; empty if it was not.
	const std::string &Error() const
	{
		return m_error;
	}
	// The real seconds the last jump took, from its Begin to the effect lifting, and of those the ones
	// it held the effect over a shown destination for its resources: what a slow load costs.
	double Seconds() const
	{
		return m_seconds;
	}
	double StreamingSeconds() const
	{
		return m_streamingSeconds;
	}

  private:
	// Both files read: parse and check them, or abandon the jump.
	void Read();
	void Abandon(std::string error);
	void ReleaseFiles();

	Resource::ResourceManager &m_resources;
	Show m_show;
	JumpSettings m_settings;

	JumpStage m_stage{JumpStage::Idle};
	// How far the effect has risen, from 0 to 1, before it is eased.
	double m_cover{0.0};
	std::string m_error;
	double m_seconds{0.0};
	double m_streamingSeconds{0.0};
	// Frames drawn of the shown destination, so the stream is not judged before one asked for it.
	int m_framesShown{0};

	// The jump under way.
	const Celestial::Galaxy *m_pGalaxy{nullptr};
	std::size_t m_destination{0};
	std::string m_arrivalPoint;
	Resource::ResourceHandle<TextFile> m_systemFile;
	Resource::ResourceHandle<TextFile> m_sceneFile;
	std::optional<JumpArrival> m_arrival;
};
} // namespace Jettison::Scene
