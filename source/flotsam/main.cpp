#include <RenderApplication.h>

// Flotsam is the editor entry point and uses the shared renderer and sample assets.
int main(int argc, char **argv)
{
	return Jettison::Renderer::RunRenderApplication(
		argc, argv, "Flotsam", JETTISON_ASSET_SUBDIRECTORY);
}