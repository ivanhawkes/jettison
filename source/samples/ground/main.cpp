#include <jettison/app/RenderApplication.h>
#include <jettison/sample/GroundGame.h>

#include <memory>

// The ground sample's entry point, and the smoke test's game of no space: the shell with the ground
// sample's world started through a session and its presentation composed over it, the engine's assets
// and the sample's content over them.


int main(int argc, char **argv)
{
	Jettison::Sample::GroundGame game;
	return Jettison::App::RunRenderApplication(
	    argc, argv, "Ground", JETTISON_ASSET_SUBDIRECTORY,
	    std::make_unique<Jettison::Sample::GroundWorld>(), &game, JETTISON_CONTENT_SUBDIRECTORY);
}
