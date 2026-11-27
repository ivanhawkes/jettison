#include <jettison/app/RenderApplication.h>


// The experimentation harness and the renderer smoke test: the shell with no game, over the engine's
// test content (source/test/content), whose static scenes the reference frames are taken in.
int main(int argc, char **argv)
{
	return Jettison::App::RunRenderApplication(
	    argc, argv, "Test", JETTISON_ASSET_SUBDIRECTORY, nullptr, nullptr,
	    JETTISON_CONTENT_SUBDIRECTORY);
}
