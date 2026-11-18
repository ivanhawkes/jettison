cmake_minimum_required(VERSION 3.28)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

message (STATUS "CMAKE_CXX_COMPILER_VERSION = ${CMAKE_CXX_COMPILER_VERSION}")

add_subdirectory("external")

# PROJECT_ dirs, not CMAKE_ dirs: when the game fetches the engine, CMAKE_SOURCE_DIR is the game's
# tree and has no assets directory.
set(JETTISON_ASSET_BINARY_DIR "${PROJECT_BINARY_DIR}/assets")

function(jettison_use_shared_assets target)
    file(RELATIVE_PATH asset_subdirectory "${CMAKE_CURRENT_BINARY_DIR}" "${JETTISON_ASSET_BINARY_DIR}")
    target_compile_definitions(${target} PRIVATE
        "JETTISON_ASSET_SUBDIRECTORY=\"${asset_subdirectory}\"")
    add_dependencies(${target} jettison_assets)
endfunction()

# Stages a tree of assets as a target, for the executables that mount it and for packing
# (jettison_add_pack): the DIRECTORIES under SOURCE copied to OUTPUT when the tree is configured
# and again by every build of the target, so an edit to a scene, a clip or a material reaches the
# build's copy, which the running app reads and hot-reloads from; the CONFIGURED directories, large
# and rarely changed (sounds, fonts, models), at configure time only, a file added to them
# reconfiguring the tree; and FILES, paths under SOURCE (a package's mod.yaml), at both. The target
# keeps OUTPUT as JETTISON_ASSET_DIRECTORY, which jettison_use_content reads.
function(jettison_add_assets target)
	cmake_parse_arguments(PARSE_ARGV 1 assets "" "SOURCE;OUTPUT" "DIRECTORIES;CONFIGURED;FILES")
	if(NOT assets_SOURCE OR NOT assets_OUTPUT)
		message(FATAL_ERROR "jettison_add_assets(${target}) needs SOURCE and OUTPUT")
	endif()
	set(globs)
	foreach(directory IN LISTS assets_DIRECTORIES assets_CONFIGURED)
		list(APPEND globs "${assets_SOURCE}/${directory}/*")
		file(COPY "${assets_SOURCE}/${directory}" DESTINATION "${assets_OUTPUT}")
	endforeach()
	file(GLOB_RECURSE staged CONFIGURE_DEPENDS ${globs})
	set(commands)
	foreach(directory IN LISTS assets_DIRECTORIES)
		list(APPEND commands COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${assets_SOURCE}/${directory}" "${assets_OUTPUT}/${directory}")
	endforeach()
	foreach(path IN LISTS assets_FILES)
		cmake_path(GET path PARENT_PATH parent)
		file(COPY "${assets_SOURCE}/${path}" DESTINATION "${assets_OUTPUT}/${parent}")
		list(APPEND commands COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${assets_SOURCE}/${path}" "${assets_OUTPUT}/${path}")
	endforeach()
	add_custom_target(${target} ${commands} VERBATIM)
	set_property(TARGET ${target} PROPERTY JETTISON_ASSET_DIRECTORY "${assets_OUTPUT}")
endfunction()

# A game's content (a jettison_add_assets target) for the executable to mount over the engine's
# assets, as "base", by default: its staged tree, or the pack of it beside it as a shipped build
# has. --content names another.
function(jettison_use_content target content)
	get_property(directory TARGET ${content} PROPERTY JETTISON_ASSET_DIRECTORY)
	file(RELATIVE_PATH subdirectory "${CMAKE_CURRENT_BINARY_DIR}" "${directory}")
	target_compile_definitions(${target} PRIVATE "JETTISON_CONTENT_SUBDIRECTORY=\"${subdirectory}\"")
	add_dependencies(${target} ${content})
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

if(JETTISON_PRESENTATION)
	add_subdirectory("${PROJECT_SOURCE_DIR}/assets" "${JETTISON_ASSET_BINARY_DIR}")
endif()

add_subdirectory("engine")

add_subdirectory("tools/pack")

if(JETTISON_PRESENTATION)
	add_subdirectory("tools/flotsam")
	# The space-trading sample, a game composed into the shell, and the runtime viewer that runs it.
	add_subdirectory("samples/space")
	add_subdirectory("samples/jetsam")
	# The render smoke harness and the dependency probe.
	add_subdirectory("test")
endif()
