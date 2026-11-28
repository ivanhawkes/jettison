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

// A screen of the elements a stylesheet styles: what the engine's own documents are made of.
constexpr std::string_view kDocument = R"(<rml><head><link type="text/rcss" href="fuzz.rcss"/></head>
<body class="screen"><div id="panel" class="panel"><h1>Saves</h1><p class="note">A <em>note</em>.</p>
<button id="load" class="primary">Load</button><input type="text" value="name"/>
<select><option value="a">A</option></select><progress value="0.5"/><img src="none.png"/>
<table><tr><td>1</td><td>2</td></tr></table></div></body></rml>)";
} // namespace


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A stylesheet over a fixed screen, loaded by a UI of its own over the engine's assets, laid out and
// drawn twice, a second apart.
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
	scratch.Put("ui/fuzz.rml", std::as_bytes(std::span{kDocument.data(), kDocument.size()}));
	scratch.Put("ui/fuzz.rcss", Fuzz::Bytes(pData, size));
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
