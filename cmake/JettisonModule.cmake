# jettison_add_module(<name>
#     [NAMESPACE <Name>]
#     [DETERMINISTIC]
#     [HEADERS <file>...]
#     [SOURCES <file>...]
#     [TESTS <file>...]
#     [DEPENDS <module>...]
#     [PUBLIC_LIBRARIES <target>...]
#     [PRIVATE_LIBRARIES <target>...])
#
# One engine module: the static library jettison_<name>, its alias Jettison::<Name>, the public
# header set under include/ (#include <jettison/<name>/Thing.h>), src/ as the private include
# path, the project's warnings, and a doctest executable registered with CTest. Paths are relative
# to the module's directory. NAMESPACE defaults to the name with its first letter capitalised, and
# is the alias, the exported name and the C++ namespace under Jettison::.
#
# DEPENDS names engine modules, which become public dependencies, and each one must already exist.
# CMake links a cycle between static libraries without complaint, so the order modules are added
# in is the dependency graph's topological order, and an edge that points at a module not yet
# added - upward, or sideways to a sibling added later - stops the configure. See
# docs/architecture.md for the tiers.
#
# DETERMINISTIC marks a simulation module (docs/coding-standards.md, "Simulation code"): it
# compiles with -ffp-contract=off, so whether a multiply and an add fuse into one FMA does not
# depend on the optimisation level.
function(jettison_add_module name)
	cmake_parse_arguments(PARSE_ARGV 1 arg
		"DETERMINISTIC"
		"NAMESPACE"
		"HEADERS;SOURCES;TESTS;DEPENDS;PUBLIC_LIBRARIES;PRIVATE_LIBRARIES")
	if(arg_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "jettison_add_module(${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
	endif()
	if(NOT arg_NAMESPACE)
		string(SUBSTRING "${name}" 0 1 first)
		string(SUBSTRING "${name}" 1 -1 rest)
		string(TOUPPER "${first}" first)
		set(arg_NAMESPACE "${first}${rest}")
	endif()

	set(target "jettison_${name}")
	add_library(${target} STATIC)
	add_library(Jettison::${arg_NAMESPACE} ALIAS ${target})
	set_target_properties(${target} PROPERTIES
		EXPORT_NAME ${arg_NAMESPACE}
		FOLDER "Engine")

	target_sources(${target}
		PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include" FILES ${arg_HEADERS}
		PRIVATE ${arg_SOURCES})
	target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

	set(module_dependencies)
	foreach(dependency IN LISTS arg_DEPENDS)
		if(NOT TARGET jettison_${dependency})
			message(FATAL_ERROR
				"jettison_add_module(${name}): depends on '${dependency}', which has not been added. "
				"Modules are added lowest tier first, so this edge points up or sideways "
				"(docs/architecture.md, Module dependencies).")
		endif()
		list(APPEND module_dependencies jettison_${dependency})
	endforeach()

	# The warnings are the project's build rule, not part of the interface: BUILD_INTERFACE keeps
	# them out of the exported targets, so the game repository compiles with its own.
	target_link_libraries(${target}
		PUBLIC ${module_dependencies} ${arg_PUBLIC_LIBRARIES}
		PRIVATE ${arg_PRIVATE_LIBRARIES} $<BUILD_INTERFACE:jettison_warnings>)

	if(arg_DETERMINISTIC)
		target_compile_options(${target} PRIVATE -ffp-contract=off)
	endif()

	set_property(GLOBAL APPEND PROPERTY JETTISON_MODULES ${target})

	if(JETTISON_BUILD_TESTS AND arg_TESTS)
		set(tests "${target}_tests")
		add_executable(${tests} ${arg_TESTS})
		target_include_directories(${tests} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
		target_link_libraries(${tests} PRIVATE ${target} doctest::doctest_with_main jettison_warnings)
		if(arg_DETERMINISTIC)
			target_compile_options(${tests} PRIVATE -ffp-contract=off)
		endif()
		set_target_properties(${tests} PROPERTIES FOLDER "Test")
		add_test(NAME ${name} COMMAND ${tests})
	endif()
endfunction()
