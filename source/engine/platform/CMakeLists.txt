jettison_add_module(platform
	DEPENDS
		core
	HEADERS
		include/jettison/platform/Cpu.h
		include/jettison/platform/Crash.h
		include/jettison/platform/Paths.h
	SOURCES
		src/Cpu.cpp
		src/Crash.cpp
		src/Paths.cpp
	TESTS
		tests/CpuTests.cpp
		tests/CrashTests.cpp
		tests/PathsTests.cpp
)

# The CPU check has to run on a machine that fails it, so its translation unit is compiled for the
# x86-64 baseline whatever else the build asks for: Jolt's public -mavx2 once phase 7 links it, or
# a developer's -march=native. Source options come after the target's on the command line, and the
# last -m flag wins.
set_source_files_properties(src/Cpu.cpp PROPERTIES
	COMPILE_OPTIONS "-mno-avx;-mno-bmi;-mno-lzcnt;-mno-popcnt;-mno-f16c")

# The guard in Cpu.cpp runs from .preinit_array, before any static initialiser, so nothing calls
# it: without this, an executable that never names a symbol from Cpu.cpp would not pull the object
# out of the archive, and would start without the check. --require-defined rather than --undefined,
# so an executable that somehow lacks the guard fails to link instead of starting without it.
target_link_options(jettison_platform INTERFACE "LINKER:--require-defined=JettisonCpuGuard")
