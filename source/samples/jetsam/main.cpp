#include <jettison/app/RenderApplication.h>

// Jetsam is the runtime entry point and uses the shared renderer and sample assets.

int main(int argc, char **argv)
{
	return Jettison::App::RunRenderApplication(argc, argv, "Jetsam", JETTISON_ASSET_SUBDIRECTORY);
}