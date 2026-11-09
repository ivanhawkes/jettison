file(GLOB_RECURSE JETTISON_ASSET_FILES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/fonts/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/models/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/textures/*"
)
file(COPY fonts models textures DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

add_custom_target(jettison_assets)
jettison_compile_shaders(jettison_assets shaders/shader.vert shaders/shader.frag)