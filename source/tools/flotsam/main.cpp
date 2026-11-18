#include <jettison/app/RenderApplication.h>


// Flotsam is the editor entry point: the shell with no game composed into it, on the shared renderer
// and assets. No market, trader or fleet of the sample is registered.
int main(int argc, char **argv)
{
	return Jettison::App::RunRenderApplication(argc, argv, "Flotsam", JETTISON_ASSET_SUBDIRECTORY);
}