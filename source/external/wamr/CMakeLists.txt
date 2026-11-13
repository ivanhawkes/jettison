# The WebAssembly Micro Runtime, built from its embedding script (build-scripts/runtime_lib.cmake)
# rather than its top-level application build. This directory exists to contain that script:
# it appends to CMAKE_C_FLAGS and calls add_definitions, both of which apply to the directory
# that runs it, so here they reach this library and nothing else.
#
# The configuration is the smallest runtime the script host (engine/script, phase 11b) needs: the
# fast interpreter, and no ahead-of-time or just-in-time compilation, libc, WASI, threads or SIMD.
# WASI would hand scripts file and clock access that the host API grants capability by capability;
# the built-in libc subset registers printf, malloc and the rest under "env" for any module to
# import, where a script imports nothing but the host's own functions; and SIMD in the fast
# interpreter brings SIMDe, a further download.
# On top of that:
#  - Bulk memory and reference types, which clang and rustc both use by default for wasm32 (an
#    indirect call, as Rust's formatting makes, is encoded with reference types), so a script built
#    with either toolchain's defaults loads.
#  - Instruction metering, the fuel: each call runs at most so many of the interpreter's
#    instructions, counted the same on every run, so a script out of fuel stops at the same point
#    in every build (docs/architecture.md, "Scripts").
#  - The thread manager, alone, without the pthread library it usually comes with: it is what lets
#    another thread stop a call. The host's watchdog terminates a call past its time limit, and the
#    interpreter only sees that at a branch or a call when the call's environment is in a cluster,
#    which the thread manager makes for it.
#  - Software bounds checks. WAMR's hardware checks reserve 8 GiB of address space an instance and
#    catch an access out of bounds with a SIGSEGV handler of its own, which would sit under the
#    sanitisers and under the crash handler phase 11d installs. A check in the interpreter costs a
#    compare an access, and the handler is never installed. The same goes for waking a blocking
#    operation, which signals the thread, and scripts have no blocking operation to wake.
#  - Its messages through the engine's log: os_printf calls jettison_wamr_vprintf (print.cpp), which
#    the script module points at Log.h.

set(WAMR_ROOT_DIR "${wamr_SOURCE_DIR}")
set(WAMR_BUILD_PLATFORM linux)
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_BULK_MEMORY 1)
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_BUILD_INSTRUCTION_METERING 1)
set(WAMR_BUILD_THREAD_MGR 1)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
set(WAMR_DISABLE_WAKEUP_BLOCKING_OP 1)
set(WAMR_BH_VPRINTF jettison_wamr_vprintf)

include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")

add_library(wamr STATIC EXCLUDE_FROM_ALL ${WAMR_RUNTIME_LIB_SOURCE} print.cpp)
target_include_directories(wamr SYSTEM PUBLIC "${WAMR_ROOT_DIR}/core/iwasm/include")
target_include_directories(wamr PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# The simulation calls scripts (an AI consideration's input), so the interpreter's arithmetic is
# held to the coding standards' rule for simulation code: no multiply and add fused into one FMA at
# one optimisation level and not at another.
target_compile_options(wamr PRIVATE $<$<COMPILE_LANGUAGE:C>:-ffp-contract=off>)

# WAMR's x86-64 call trampoline (invokeNative_em64.s) has no .note.GNU-stack section, and an
# object without one makes the linker mark the whole executable as needing an executable stack -
# for every thread, not just the ones running scripts. The trampoline does not execute code on
# the stack, so it is assembled as not needing one.
target_compile_options(wamr PRIVATE $<$<COMPILE_LANGUAGE:ASM>:-Wa,--noexecstack>)

find_package(Threads REQUIRED)
target_link_libraries(wamr PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
