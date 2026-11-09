#include <RenderApplication.h>

// Test is the experimentation harness and supports the renderer smoke test.
int main(int argc, char **argv)
{
	return Jettison::Renderer::RunRenderApplication(argc, argv, "Test", JETTISON_ASSET_SUBDIRECTORY);
}
