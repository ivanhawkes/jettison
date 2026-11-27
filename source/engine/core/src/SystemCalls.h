#pragma once

#include <sys/types.h>

namespace Jettison::Core
{
// The system calls the durable writes in File.cpp make, through one table so that a test can make
// each fail in turn (tests/FileTests.cpp): a working disk does not fail on demand. Production code
// never changes it, and a call through a pointer costs nothing beside the call it makes. Not thread
// safe while a test has it changed.
struct SystemCalls
{
	int (*open)(const char *path, int flags, mode_t mode);
	ssize_t (*write)(int descriptor, const void *bytes, size_t count);
	int (*fsync)(int descriptor);
	int (*close)(int descriptor);
	int (*rename)(const char *from, const char *to);
	int (*renameat2)(
	    int fromDirectory, const char *from, int toDirectory, const char *to, unsigned flags);
	int (*mkdir)(const char *path, mode_t mode);
};

// The table File.cpp calls through, the system's own calls until a test changes one.
SystemCalls &FileSystemCalls();
} // namespace Jettison::Core
