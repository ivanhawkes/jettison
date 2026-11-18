#include <jettison/app/RenderApplication.h>
#include <jettison/sample/SpaceGame.h>


// Test is the experimentation harness and supports the renderer smoke test, whose stages walk the
// space-trading sample's market screen.
int main(int argc, char **argv)
{
	Jettison::Sample::SpaceGame game;
	return Jettison::App::RunRenderApplication(argc, argv, "Test", JETTISON_ASSET_SUBDIRECTORY, &game);
}
