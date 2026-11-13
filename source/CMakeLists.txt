cmake_minimum_required(VERSION 3.28)

# Set the overall project name.
# Turn on using solution folders.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# I like to know which compiler I am using.
message (STATUS "CMAKE_CXX_COMPILER_VERSION = ${CMAKE_CXX_COMPILER_VERSION}")

option(PLUGIN_IMGUI "Enable the imgui plugin" ON)
if (NOT PLUGIN_IMGUI)
    return()
endif()

add_subdirectory("external")

# PROJECT_ dirs, not CMAKE_ dirs: when the game fetches the engine, CMAKE_SOURCE_DIR is the game's
# tree and has no assets directory. The consumer check (source/test/consumer) found this.
set(JETTISON_ASSET_BINARY_DIR "${PROJECT_BINARY_DIR}/assets")

function(jettison_use_shared_assets target)
    file(RELATIVE_PATH asset_subdirectory "${CMAKE_CURRENT_BINARY_DIR}" "${JETTISON_ASSET_BINARY_DIR}")
    target_compile_definitions(${target} PRIVATE
        "JETTISON_ASSET_SUBDIRECTORY=\"${asset_subdirectory}\"")
    add_dependencies(${target} jettison_assets)
endfunction()

# Packs a directory into a pack file (vfs/Pack.h), which the VFS mounts as it would the directory,
# when the target is built: a game's assets for shipping, a mod for its players. DIRECTORIES names
# the directories under SOURCE to pack, every file under it when none are named; DEPENDS, the
# targets that stage what it packs. The packer runs whenever the target is built, since what it
# reads is a tree another target writes; for the sample assets, about a second.
function(jettison_add_pack target)
	cmake_parse_arguments(PARSE_ARGV 1 pack "" "SOURCE;OUTPUT;LEVEL" "DIRECTORIES;DEPENDS")
	if(NOT pack_SOURCE OR NOT pack_OUTPUT)
		message(FATAL_ERROR "jettison_add_pack(${target}) needs SOURCE and OUTPUT")
	endif()
	set(flags)
	if(pack_LEVEL)
		list(APPEND flags --level ${pack_LEVEL})
	endif()
	foreach(directory IN LISTS pack_DIRECTORIES)
		list(APPEND flags --directory ${directory})
	endforeach()
	add_custom_target(${target}
		COMMAND jettison_pack "${pack_SOURCE}" "${pack_OUTPUT}" ${flags}
		BYPRODUCTS "${pack_OUTPUT}"
		COMMENT "Packing ${pack_SOURCE} into ${pack_OUTPUT}"
		VERBATIM
	)
	add_dependencies(${target} jettison_pack ${pack_DEPENDS})
endfunction()

# Compiles a script (docs/architecture.md, "Scripts") into a wasm32 module at OUTPUT, when the
# target is built, from C or from Rust, against the host API the game generates (jettison_script.h,
# jettison_script.rs), which INCLUDES finds for C and a Rust crate takes in by #[path] beside it.
# ALL builds it with the tree, as a test's modules are.
#  - C: each source compiled freestanding by clang, and the objects linked by wasm-ld. Nix's compiler
#    wrapper cannot compile for a second target (it adds the host's hardening flags, and its own
#    target unless it warns about ours), so under it the compiler it wraps is used, which its
#    nix-support/orig-cc names.
#  - Rust: one source, the crate's root, a no_std cdylib for wasm32-unknown-unknown with
#    panic=abort, linked by the same wasm-ld.
# Both link with a 16 KiB stack rather than wasm-ld's 64 KiB, so a script starts in fewer pages, and
# with the stack first, below the data, as rustc links it: a stack that overflows then runs off the
# bottom of memory and traps, where wasm-ld's default for C puts it above the data, into which it
# would run without a word.
find_program(JETTISON_WASM_LD wasm-ld)
find_program(JETTISON_RUSTC rustc)
set(JETTISON_SCRIPT_COMPILER "${CMAKE_C_COMPILER}")
get_filename_component(compiler_directory "${CMAKE_C_COMPILER}" DIRECTORY)
if(EXISTS "${compiler_directory}/../nix-support/orig-cc")
	file(READ "${compiler_directory}/../nix-support/orig-cc" wrapped)
	string(STRIP "${wrapped}" wrapped)
	set(JETTISON_SCRIPT_COMPILER "${wrapped}/bin/clang")
endif()
function(jettison_add_script target)
	cmake_parse_arguments(PARSE_ARGV 1 script "ALL" "OUTPUT" "SOURCES;INCLUDES")
	if(NOT script_OUTPUT OR NOT script_SOURCES)
		message(FATAL_ERROR "jettison_add_script(${target}) needs OUTPUT and SOURCES")
	endif()
	set(all)
	if(script_ALL)
		set(all ALL)
	endif()
	list(GET script_SOURCES 0 first)
	get_filename_component(language "${first}" LAST_EXT)
	set(missing)
	if(NOT JETTISON_WASM_LD)
		set(missing "wasm-ld (lld)")
	elseif(language STREQUAL ".rs" AND NOT JETTISON_RUSTC)
		set(missing "rustc")
	endif()
	if(missing)
		add_custom_target(${target} ${all}
			COMMAND ${CMAKE_COMMAND} -E echo "${target}: no ${missing} to build scripts with (the devenv shell has it)"
			COMMAND ${CMAKE_COMMAND} -E false
			VERBATIM)
		return()
	endif()

	if(language STREQUAL ".rs")
		list(LENGTH script_SOURCES count)
		if(NOT count EQUAL 1)
			message(FATAL_ERROR "jettison_add_script(${target}): a Rust script is one source, its crate's root")
		endif()
		add_custom_command(
			OUTPUT "${script_OUTPUT}"
			COMMAND ${JETTISON_RUSTC} --edition 2024 --target wasm32-unknown-unknown --crate-type cdylib
				-C opt-level=2 -C panic=abort -D warnings
				-C linker=${JETTISON_WASM_LD} -C link-arg=-zstack-size=16384
				--emit=link,dep-info=${script_OUTPUT}.d -o "${script_OUTPUT}" "${first}"
			DEPENDS "${first}"
			DEPFILE "${script_OUTPUT}.d"
			COMMENT "Compiling the script ${first}"
			VERBATIM)
		add_custom_target(${target} ${all} DEPENDS "${script_OUTPUT}")
		return()
	endif()

	set(includes)
	foreach(directory IN LISTS script_INCLUDES)
		list(APPEND includes -I${directory})
	endforeach()
	set(objects)
	foreach(source IN LISTS script_SOURCES)
		get_filename_component(stem "${source}" NAME_WE)
		set(object "${CMAKE_CURRENT_BINARY_DIR}/${target}.dir/${stem}.o")
		add_custom_command(
			OUTPUT "${object}"
			COMMAND ${JETTISON_SCRIPT_COMPILER} --target=wasm32 -std=c17 -O2 -ffreestanding -nostdlib
				-Wall -Wextra -Werror ${includes}
				-MD -MF "${object}.d" -c "${source}" -o "${object}"
			DEPENDS "${source}"
			DEPFILE "${object}.d"
			COMMENT "Compiling the script ${source}"
			VERBATIM)
		list(APPEND objects "${object}")
	endforeach()
	add_custom_command(
		OUTPUT "${script_OUTPUT}"
		COMMAND ${JETTISON_WASM_LD} --no-entry --stack-first -z stack-size=16384 -o "${script_OUTPUT}" ${objects}
		DEPENDS ${objects}
		COMMENT "Linking the script ${script_OUTPUT}"
		VERBATIM)
	add_custom_target(${target} ${all} DEPENDS "${script_OUTPUT}")
endfunction()

add_subdirectory("${PROJECT_SOURCE_DIR}/assets" "${JETTISON_ASSET_BINARY_DIR}")

# The engine modules.
add_subdirectory("engine")

# The editor, and the packer.
add_subdirectory("tools/flotsam")
add_subdirectory("tools/pack")

# The runtime viewer: the engine with no game.
add_subdirectory("samples/jetsam")

# The render smoke harness and the dependency probe.
add_subdirectory("test")
