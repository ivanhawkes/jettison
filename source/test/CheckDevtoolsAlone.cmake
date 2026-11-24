# The devtools_alone test: the executable's NEEDED names no Vulkan loader or GLFW, and it runs.
execute_process(COMMAND "${READELF}" -d "${EXECUTABLE}" OUTPUT_VARIABLE dynamic COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCHALL "\\(NEEDED\\)[^\n]*" needed "${dynamic}")
# None at all would be readelf's format changing under the match, not a pass.
if(NOT needed)
	message(FATAL_ERROR "readelf -d ${EXECUTABLE} listed no NEEDED entries")
endif()
message(STATUS "${needed}")
if(needed MATCHES "libvulkan|libglfw")
	message(FATAL_ERROR "${EXECUTABLE} needs a Vulkan loader or GLFW: ${needed}")
endif()
execute_process(COMMAND "${EXECUTABLE}" COMMAND_ERROR_IS_FATAL ANY)
