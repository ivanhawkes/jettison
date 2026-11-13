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
