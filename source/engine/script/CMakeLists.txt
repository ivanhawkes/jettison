jettison_add_module(script
	DETERMINISTIC
	DEPENDS
		core
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

# The suite's modules, compiled from tests/scripts against the bindings the suite's own types generate
# (checked there, as tests/scripts/jettison_script.h and jettison_script.rs): built with the tree rather than as the suite's
# dependency, so a change to the types still builds the suite that says the header is out of date.
if(TARGET jettison_script_tests)
	set(script_test_modules "${CMAKE_CURRENT_BINARY_DIR}/test-scripts")
	# Rust, as scripts are written for the shell, and the host API's module in C as well, the same
	# calls made from each: <module> <source>.
	set(modules limits limits.rs rogue rogue.rs events events.c events_rs events.rs keys keys.c keys_rs
		keys.rs)
	while(modules)
		list(POP_FRONT modules name source)
		jettison_add_script(jettison_script_test_${name} ALL
			OUTPUT "${script_test_modules}/${name}.wasm"
			SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/${source}"
			INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts")
	endwhile()
	target_compile_definitions(jettison_script_tests PRIVATE
		JETTISON_SCRIPT_TEST_MODULES="${script_test_modules}"
		JETTISON_SCRIPT_TEST_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts"
		JETTISON_SCRIPT_TEST_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}")
endif()
