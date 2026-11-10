file(GLOB_RECURSE JETTISON_ASSET_FILES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/fonts/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/models/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/scene/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/systems/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/textures/*"
)
# Git cannot record an empty directory, and 90f4a96 removed the last textures, so a fresh
# clone has no assets/textures and the old 'file(COPY fonts models scene textures ...)'
# failed configure there; the tree built only because the empty directory survived locally.
# Copy the directories that exist, then create the empty one so the runtime layout is the
# same in every tree.
foreach(asset_dir IN ITEMS fonts models scene systems)
	file(COPY "${asset_dir}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/textures")

add_custom_target(jettison_assets
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/scene" "${CMAKE_CURRENT_BINARY_DIR}/scene"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/systems" "${CMAKE_CURRENT_BINARY_DIR}/systems"
)
jettison_compile_shaders(jettison_assets
	shaders/mesh.vert
	shaders/mesh.frag
	shaders/output.vert
	shaders/output.frag
)