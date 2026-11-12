file(GLOB_RECURSE JETTISON_ASSET_FILES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/audio/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/defs/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/fonts/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/materials/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/models/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/scene/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.program.yaml"
	"${CMAKE_CURRENT_SOURCE_DIR}/systems/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/textures/*"
	"${CMAKE_CURRENT_SOURCE_DIR}/ui/*"
)
# The asset directories are copied when the tree is configured and again by every build, so an
# edit to a scene, a material or a texture reaches the build's copy, which the running app reads
# and hot-reloads from. Sounds, fonts and models are copied at configure time only: they are large
# and rarely change.
foreach(asset_dir IN ITEMS audio defs fonts materials models scene systems textures ui)
	file(COPY "${asset_dir}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endforeach()
file(GLOB JETTISON_PROGRAM_DESCRIPTORS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.program.yaml")
file(COPY ${JETTISON_PROGRAM_DESCRIPTORS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set(JETTISON_COPIED_ASSET_DIRS defs materials scene systems textures ui)
set(copy_commands)
foreach(asset_dir IN LISTS JETTISON_COPIED_ASSET_DIRS)
	list(APPEND copy_commands COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/${asset_dir}" "${CMAKE_CURRENT_BINARY_DIR}/${asset_dir}")
endforeach()
add_custom_target(jettison_assets
	${copy_commands}
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${JETTISON_PROGRAM_DESCRIPTORS} "${CMAKE_CURRENT_BINARY_DIR}/shaders"
)
jettison_compile_shaders(jettison_assets
	shaders/debug_line.vert
	shaders/debug_line.frag
	shaders/mesh.vert
	shaders/mesh.frag
	shaders/atmosphere.frag
	shaders/brdf_lut.frag
	shaders/cluster_bounds.comp
	shaders/cluster_lights.comp
	shaders/output.frag
	shaders/overlay.vert
	shaders/overlay.frag
	shaders/planet.frag
	shaders/sky.frag
	shaders/unlit.frag
)
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/mesh.vert OUTPUT shaders/mesh_triplanar.vert DEFINES TRIPLANAR)
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/mesh.frag OUTPUT shaders/mesh_triplanar.frag DEFINES TRIPLANAR)
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/mesh.vert OUTPUT shaders/unlit.vert DEFINES UNLIT)
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/mesh.vert OUTPUT shaders/planet.vert DEFINES PLANET)
# The sun's shadow pass: the mesh vertex stage alone, into a cascade.
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/mesh.vert OUTPUT shaders/shadow_depth.vert DEFINES SHADOW_DEPTH)
# The opaque depth prepass: the mesh vertex stage alone, into the view.
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/mesh.vert OUTPUT shaders/depth_prepass.vert DEFINES DEPTH_PREPASS)
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/sky.frag OUTPUT shaders/sky_capture.frag DEFINES CAPTURE)
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/prefilter.frag OUTPUT shaders/prefilter_specular.frag DEFINES SPECULAR)
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/prefilter.frag OUTPUT shaders/prefilter_irradiance.frag)
# Every full-screen program's vertex stage is the one triangle.
foreach(program IN ITEMS output sky sky_capture atmosphere brdf_lut prefilter_specular
		prefilter_irradiance)
	jettison_compile_shader_variant(jettison_assets
		SOURCE shaders/fullscreen.vert OUTPUT shaders/${program}.vert)
endforeach()
