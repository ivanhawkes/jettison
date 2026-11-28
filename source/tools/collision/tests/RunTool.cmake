# The bake_collision_cli test: the tool bakes a model twice to the same bytes, and fails on a model
# it cannot read.
#
#   cmake -DTOOL=<jettison_bake_collision> -DMODELS=<tests/models> -DOUTPUT=<directory> -P <this>

file(REMOVE_RECURSE "${OUTPUT}")
foreach(copy IN ITEMS first second)
	execute_process(
		COMMAND "${TOOL}" "${MODELS}/pods.gltf" "${OUTPUT}/${copy}/pods.collision"
		RESULT_VARIABLE result)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "jettison_bake_collision failed on pods.gltf: ${result}")
	endif()
endforeach()
execute_process(
	COMMAND ${CMAKE_COMMAND} -E compare_files
		"${OUTPUT}/first/pods.collision" "${OUTPUT}/second/pods.collision"
	RESULT_VARIABLE differ)
if(NOT differ EQUAL 0)
	message(FATAL_ERROR "jettison_bake_collision baked pods.gltf to two different files")
endif()

execute_process(
	COMMAND "${TOOL}" "${MODELS}/none.gltf" "${OUTPUT}/none.collision"
	RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0 OR EXISTS "${OUTPUT}/none.collision")
	message(FATAL_ERROR "jettison_bake_collision baked a model that is not there")
endif()
