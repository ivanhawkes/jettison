#include <jettison/app/RenderApplication.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "Application.h"
#include <jettison/app/AppArguments.h>

namespace Jettison::App
{
// The entry point flotsam, jetsam and render_test all call: read the command line, run the shell,
// report what the run said. Everything that has a lifetime belongs to Application, whose destructor
// is the lifecycle's safety net: constructed inside the try block, it tears the engine down in
// order on every path - the frame loop's exit, an exception thrown from a frame, a start-up that
// failed - before this function's handler prints.
int RunRenderApplication(
    int argc, char **argv, const char *windowTitle, const std::filesystem::path &assetSubdirectory)
{
	const auto arguments = ParseAppArguments(argc, argv, assetSubdirectory);
	if (!arguments)
		return EXIT_FAILURE;

	try
	{
		Application application{*arguments, windowTitle};
		application.Run();
	}
	catch (const std::exception &e)
	{
		// A start-up that failed, a frame that could not be presented, a headless sequence that
		// did not reach its last stage: the message is the run's report, and the exit status is
		// what the gate reads.
		std::cerr << e.what() << '\n';
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
} // namespace Jettison::App
