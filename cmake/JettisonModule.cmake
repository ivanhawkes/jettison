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

	# Into the package's export set, headers with it. The private include path is PRIVATE and the
	# warnings are BUILD_INTERFACE above, so neither reaches the export.
	if(JETTISON_INSTALL)
		include(GNUInstallDirs)
		install(TARGETS ${target} EXPORT JettisonTargets
			FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
	endif()

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


# jettison_check_optional(SAMPLES <directory> OPTIONAL <module>... USERS <module>...)
#
# Holds the modules to their dependencies (docs/architecture.md, "Module dependencies"), called once
# every target is added: it stops the configure when a module has a target defined under SAMPLES in
# its link closure, whatever the target is called; or when one neither optional nor a user has an
# optional module in its closure, reached through any target, or includes one's headers in its own
# public headers, where a consumer would need it; or when a link of any target in a module's closure
# has a generator expression anywhere in it that the check cannot read a target from, since a
# helper's conditional link would otherwise drop out of the closure. A module is held to it unless
# it is listed, so a new one is from the start. The suites are not modules: a neutral module's may
# fly in a star system.
function(jettison_check_optional)
	cmake_parse_arguments(PARSE_ARGV 0 arg "" "SAMPLES" "OPTIONAL;USERS")
	set(listed)
	foreach(module IN LISTS arg_OPTIONAL arg_USERS)
		list(APPEND listed jettison_${module})
	endforeach()
	list(JOIN arg_OPTIONAL "|" optional)
	list(JOIN arg_USERS ", " users)
	get_property(modules GLOBAL PROPERTY JETTISON_MODULES)
	set(wrappers "BUILD_INTERFACE|BUILD_LOCAL_INTERFACE|INSTALL_INTERFACE|LINK_ONLY|TARGET_NAME")
	foreach(target IN LISTS modules)
		# Every target it links, through every one of them, whatever its name: a sample's library or
		# a helper that carries an optional module need not be named jettison_*. A link wrapped in a
		# generator expression that only says where it applies is the target inside it.
		set(closure)
		set(pending ${target})
		while(pending)
			list(POP_FRONT pending current)
			get_target_property(private ${current} LINK_LIBRARIES)
			get_target_property(public ${current} INTERFACE_LINK_LIBRARIES)
			set(links ${private} ${public})
			while(links)
				list(POP_FRONT links link)
				while(link MATCHES "^\\$<(${wrappers}):(.*)>$")
					set(link "${CMAKE_MATCH_2}")
				endwhile()
				# A group or a library feature: the targets after the feature's name.
				if(link MATCHES "^\\$<(LINK_GROUP|LINK_LIBRARY):[^,>]*,(.*)>$")
					string(REPLACE "," ";" grouped "${CMAKE_MATCH_2}")
					list(PREPEND links ${grouped})
					continue()
				endif()
				if(link MATCHES "\\$<")
					message(FATAL_ERROR
						"${current} links '${link}', an expression the dependency check cannot follow; "
						"name the target (docs/architecture.md, Module dependencies).")
				endif()
				if(NOT TARGET "${link}")
					continue()
				endif()
				get_target_property(aliased ${link} ALIASED_TARGET)
				if(aliased)
					set(link ${aliased})
				endif()
				if(NOT link IN_LIST closure)
					list(APPEND closure ${link})
					list(APPEND pending ${link})
				endif()
			endwhile()
		endwhile()

		foreach(linked IN LISTS closure)
			get_target_property(directory ${linked} SOURCE_DIR)
			cmake_path(IS_PREFIX arg_SAMPLES "${directory}" NORMALIZE sample)
			if(sample)
				message(FATAL_ERROR
					"${target} depends on ${linked}, a sample's, directly or through another module; "
					"no engine module may (docs/architecture.md, Module dependencies).")
			endif()
		endforeach()

		if(target IN_LIST listed)
			continue()
		endif()
		foreach(module IN LISTS arg_OPTIONAL)
			if(jettison_${module} IN_LIST closure)
				message(FATAL_ERROR
					"${target} depends on the optional '${module}', directly or through another module; "
					"only ${users} may (docs/architecture.md, Module dependencies).")
			endif()
		endforeach()

		get_target_property(directory ${target} SOURCE_DIR)
		get_target_property(headers ${target} HEADER_SET)
		foreach(header IN LISTS headers)
			if(NOT IS_ABSOLUTE "${header}")
				set(header "${directory}/${header}")
			endif()
			file(STRINGS "${header}" includes REGEX "#[ \t]*include[ \t]*<jettison/(${optional})/")
			if(includes)
				message(FATAL_ERROR
					"${header}, a public header of ${target}, has '${includes}', a header of an optional "
					"module (docs/architecture.md, Module dependencies).")
			endif()
		endforeach()
	endforeach()
endfunction()
