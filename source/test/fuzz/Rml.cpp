#include <jettison/ui/UiSystem.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

#include "Fuzz.h"

using namespace Jettison;

namespace
{
constexpr int kFrameWidth = 640;
constexpr int kFrameHeight = 360;
constexpr double kLaterSeconds = 1.0;
} // namespace


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A document loaded by a UI of its own over the engine's assets, shown, laid out and drawn twice, a
// second apart, so its transitions and animations run.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	static const Fuzz::Scratch scratch{"ui"};
	static const Vfs::FileSystem files = []
	{
		Vfs::FileSystem mounted;
		mounted.AddMount({"assets", JETTISON_ASSET_SOURCE_DIR, false});
		mounted.AddMount({"scratch", scratch.Directory(), false});
		return mounted;
	}();
	scratch.Put("ui/fuzz.rml", Fuzz::Bytes(pData, size));
	Ui::UiSystem ui{files, kFrameWidth, kFrameHeight};
	ui.LoadFont("fonts/DroidSans.ttf");
	try
	{
		ui.Show(ui.LoadDocument("ui/fuzz.rml"));
	}
	catch (const std::runtime_error &)
	{
		return 0;
	}
	ui.Update(0.0);
	ui.Update(kLaterSeconds);
	return 0;
}
