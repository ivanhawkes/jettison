cmake_minimum_required (VERSION 3.17)

# Set the overall project name.
project("shaders" VERSION 0.1.0)

#add_source(shaders shader.vert)

# Move the targets into a solution folder.
#set_property(TARGET shaders PROPERTY FOLDER "Shaders")

# Find and link Vulkan.
find_package(Vulkan REQUIRED)
#target_link_libraries(shaders PRIVATE Vulkan::Vulkan)

# Copy shaders, models and the textures.
configure_file("shader.vert.spv" "shader.vert.spv" COPYONLY)
configure_file("shader.frag.spv" "shader.frag.spv" COPYONLY)
configure_file("imgui.vert.spv" "imgui.vert.spv" COPYONLY)
configure_file("imgui.frag.spv" "imgui.frag.spv" COPYONLY)

function(target_target_add_spirv_shader TARGET INPUT_FILE)
    find_program(GLSLC glslc)

    set(optionalArgs ${ARGN})
    set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

    list(LENGTH optionalArgs numArgs)
    if(${numArgs} GREATER 1)
        message(FATAL_ERROR "target_target_add_spirv_shader called incorrectly, target_add_spirv_shader(target INPUT_FILE [OUTPUT_FILE])")
    endif()
    
    if(${numArgs} EQUAL 1)
        list(GET optionalArgs 0 OUTPUT_DIR)
    endif()
    
    file(MAKE_DIRECTORY ${OUTPUT_DIR})
    get_filename_component(bare_name ${INPUT_FILE} NAME_WE)
    get_filename_component(extension ${INPUT_FILE} LAST_EXT)
    string(REGEX REPLACE "[.]+" "" extension ${extension})
    set(OUTPUT_FILE "${OUTPUT_DIR}/${bare_name}-${extension}.spv")

    add_custom_command( PRE_BUILD
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${GLSLC} ${INPUT_FILE} -o ${OUTPUT_FILE}
            MAIN_DEPENDENCY ${INPUT_FILE}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            )
    add_custom_target(${bare_name}-${extension} DEPENDS ${OUTPUT_FILE})
    add_dependencies(${TARGET} ${bare_name}-${extension})
endfunction()

#add_library(shaders STATIC main.cpp)
#set_property(TARGET shaders PROPERTY FOLDER "Shaders")
#target_target_add_spirv_shader(shaders simple-fragment.frag ${CMAKE_CURRENT_BINARY_DIR})
#target_target_add_spirv_shader(shaders shader.vert ${CMAKE_CURRENT_BINARY_DIR})


function(target_add_spirv_shader SHADER_STAGE INPUT_FILE)
	find_program(GLSLC glslc)

    # Choose an output folder and ensure it exists.
    set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY ${OUTPUT_DIR})

    get_filename_component(bare_name ${INPUT_FILE} NAME_WE)
    get_filename_component(extension ${INPUT_FILE} LAST_EXT)
    set(OUTPUT_FILE "${OUTPUT_DIR}/${bare_name}.spv")

    add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${GLSLC} -fshader-stage=${SHADER_STAGE} ${INPUT_FILE} -o ${OUTPUT_FILE}
            MAIN_DEPENDENCY ${INPUT_FILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		    )
	
	add_custom_target(${bare_name}${extension} ALL DEPENDS ${OUTPUT_FILE})
	set_property(TARGET ${bare_name}${extension} PROPERTY FOLDER "Shaders")
    #add_dependencies(${TARGET} ${bare_name}${extension})
endfunction()

#target_add_spirv_shader(vert simple-vert.vert)
#target_add_spirv_shader(fragment simple-fragment.frag)

#add_custom_target(shaders ALL DEPENDS simple-vert.spv)
#add_custom_target(shaders ALL DEPENDS simple-fragment.spv)
#set_property(TARGET shaders PROPERTY FOLDER "Shaders")
