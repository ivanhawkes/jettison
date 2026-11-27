jettison_add_module(script
	DETERMINISTIC
	DEPENDS
		core
		platform
		reflect
		serialise
		sim
		vfs
	HEADERS
		include/jettison/script/Bindings.h
		include/jettison/script/ScriptDefinition.h
		include/jettison/script/ScriptHost.h
	SOURCES
		src/Bindings.cpp
		src/ScriptDefinition.cpp
		src/ScriptHost.cpp
	PRIVATE_LIBRARIES
		wamr
	TESTS
		tests/BindingsTests.cpp
		tests/ScriptTests.cpp
)

# The suite's modules, compiled from tests/scripts against the bindings the suite's own types
# generate (checked there, as tests/scripts/jettison_script.h and jettison_script.rs): built with
# the tree rather than as the suite's dependency, so a change to the types still builds the suite
# that says the header is out of date.
if(TARGET jettison_script_tests)
	set(script_test_modules "${CMAKE_CURRENT_BINARY_DIR}/test-scripts")
	# Rust, as scripts are written for the shell, and the host API's module in C as well, the same
	# calls made from each: <module> <source>.
	set(modules limits limits.rs rogue rogue.rs events events.c events_rs events.rs keys keys.c keys_rs
		keys.rs reads reads.c reads_rs reads.rs)
	while(modules)
		list(POP_FRONT modules name source)
		jettison_add_script(jettison_script_test_${name} ALL
			OUTPUT "${script_test_modules}/${name}.wasm"
			SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/${source}"
			INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts")
	endwhile()
	# A component read kept past its read, in each of three ways, is refused as the script compiles.
	if(JETTISON_RUSTC)
		add_test(NAME script_read_kept
			COMMAND ${JETTISON_RUSTC} --edition 2024 --target wasm32-unknown-unknown --crate-type lib
				--emit=metadata -o "${CMAKE_CURRENT_BINARY_DIR}/kept.rmeta"
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/kept.rs")
		set_tests_properties(script_read_kept PROPERTIES
			PASS_REGULAR_EXPRESSION "lifetime may not live long enough.*aborting due to 3 previous errors")
	endif()
	# The suite runs scripts in a host session too, as a server does; host is above script, so the
	# suite links it rather than the module.
	target_link_libraries(jettison_script_tests PRIVATE jettison_host)
	target_compile_definitions(jettison_script_tests PRIVATE
		JETTISON_SCRIPT_TEST_MODULES="${script_test_modules}"
		JETTISON_SCRIPT_TEST_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts"
		JETTISON_SCRIPT_TEST_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}")
endif()
