# The WebAssembly Micro Runtime, built from its embedding script (build-scripts/runtime_lib.cmake)
# rather than its top-level application build. This directory exists to contain that script:
# it appends to CMAKE_C_FLAGS and calls add_definitions, both of which apply to the directory
# that runs it, so here they reach this library and nothing else.
#
# The configuration is the smallest runtime a mod host needs: the fast interpreter, the
# built-in libc subset, and no ahead-of-time or just-in-time compilation, WASI, threads or
# SIMD. WASI would hand scripts file and clock access that the mod host API is meant to grant
# capability by capability, and SIMD in the fast interpreter brings SIMDe, a further download;
# either is a decision for phase 11, not a default.

set(WAMR_ROOT_DIR "${wamr_SOURCE_DIR}")
set(WAMR_BUILD_PLATFORM linux)
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_MULTI_MODULE 0)

include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")

add_library(wamr STATIC EXCLUDE_FROM_ALL ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(wamr SYSTEM PUBLIC "${WAMR_ROOT_DIR}/core/iwasm/include")

# WAMR's x86-64 call trampoline (invokeNative_em64.s) has no .note.GNU-stack section, and an
# object without one makes the linker mark the whole executable as needing an executable stack -
# for every thread, not just the ones running scripts. The trampoline does not execute code on
# the stack, so it is assembled as not needing one.
target_compile_options(wamr PRIVATE $<$<COMPILE_LANGUAGE:ASM>:-Wa,--noexecstack>)

find_package(Threads REQUIRED)
target_link_libraries(wamr PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
