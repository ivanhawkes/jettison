#include <jettison/app/RenderApplication.h>
#include <jettison/sample/SpaceGame.h>

// Jetsam is the runtime entry point: the shell with the space-trading sample composed into it, on the
// shared renderer, the engine's assets and the sample's content over them.


int main(int argc, char **argv)
{
	Jettison::Sample::SpaceGame game;
	return Jettison::App::RunRenderApplication(
	    argc, argv, "Jetsam", JETTISON_ASSET_SUBDIRECTORY, &game, JETTISON_CONTENT_SUBDIRECTORY);
}
