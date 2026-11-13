// Where WAMR's os_printf goes (WAMR_BH_VPRINTF in CMakeLists.txt). WAMR's own would print to stdout;
// this forwards to whatever the embedder set, so the runtime's messages reach the engine's log, and
// falls back to stderr before anything is set. It is a source of the runtime's own library, so the
// symbol is there however the archives are ordered on a link line.
#include "jettison_wamr_print.h"

#include <atomic>
#include <cstdio>

namespace
{
std::atomic<jettison_wamr_print_t> g_print{nullptr};
}

extern "C" void jettison_wamr_set_print(jettison_wamr_print_t print)
{
	g_print.store(print);
}

extern "C" int jettison_wamr_vprintf(const char *format, va_list arguments)
{
	if (const jettison_wamr_print_t print = g_print.load())
		return print(format, arguments);
	return std::vfprintf(stderr, format, arguments);
}
