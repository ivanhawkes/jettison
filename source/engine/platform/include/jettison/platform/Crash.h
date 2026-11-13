#pragma once

#include <filesystem>
#include <string>

namespace Jettison::Platform
{
// Crash handling (docs/architecture.md, "Crashes"). A fatal signal - SIGSEGV, SIGBUS, SIGFPE,
// SIGILL, or SIGABRT, which a failed assertion and std::terminate end in - writes a report into a
// directory: the signal and what it says of its cause, the thread, the build and its command line,
// the stack's frames as a module and an offset into it, the same frames named by a symboliser when
// the machine has one, and the log's tail (Core::WriteLogTail). The report's head and its frames go
// to stderr as well. Then the signal is raised again under its default action, so the process ends
// as it would have, with the same status and core dump.
//
// Until the symboliser, the handler calls only what a signal handler may: it opens, reads and
// writes with system calls into buffers made as it is installed, so a crash inside malloc or with
// the log's lock held still writes its report. The frames are found by glibc's backtrace, whose
// unwinder is loaded as the handler is installed. The symboliser is a child process, started by
// vfork and exec, which run no fork handlers; the report is on the disk before it starts, and the
// process is ended by SIGALRM if the handler has not finished in 30 seconds.
//
// The handler runs on a stack of its own on the thread that installs it, so that thread's stack
// overflowing is reported too. Another thread's overflow ends the process unreported
// (docs/roadmap.md, "Carried forward").
//
// Under the address sanitiser, whose report of a fatal signal names more than this one can, nothing
// is installed.
struct CrashOptions
{
	// Where reports go, made as the handler is installed: crash-<UTC time>-<process id>.txt.
	std::filesystem::path directory;
	// The build, as the report names it: what the game is and the engine's version.
	std::string build;
	// What names the frames: addr2line, GNU's or LLVM's, or anything taking the same arguments.
	// Empty finds addr2line on PATH as the handler is installed; with none, the report's frames are
	// modules and offsets, which scripts/symbolise-crash.sh names against the build that wrote it.
	std::filesystem::path symboliser;
	bool symbolise{true};
};

// Installs the handler for the fatal signals, and a std::terminate handler that logs the exception
// that went uncaught at Fatal before it aborts. Installing again replaces the options. Returns false,
// having installed nothing, under the address sanitiser. Throws std::runtime_error naming the
// directory when it cannot be made, or a path too long for the handler's buffers. Call it from the
// main thread, before other threads are started.
bool InstallCrashHandler(const CrashOptions &options);

// Puts back the signals' actions and the terminate handler as they were before it was installed.
void UninstallCrashHandler() noexcept;
} // namespace Jettison::Platform
