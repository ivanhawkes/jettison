# The engine's own assets, what the shell needs to start and draw with no game's content mounted:
# the shaders and their programs, the fonts, the saves screen and its stylesheet, and the strings of
# the engine's modules. A game's content (the test content is source/test/content) mounts over them
# as "base" (app/Mounts.h).
file(GLOB JETTISON_PROGRAM_DESCRIPTORS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.program.yaml")
jettison_add_assets(jettison_assets
	SOURCE "${CMAKE_CURRENT_SOURCE_DIR}"
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}"
	DIRECTORIES strings ui
	CONFIGURED fonts
	FILES ${JETTISON_PROGRAM_DESCRIPTORS}
)
jettison_compile_shaders(jettison_assets
	shaders/debug_line.vert
	shaders/debug_line.frag
	shaders/depth_prepass.frag
	shaders/mesh.vert
	shaders/mesh.frag
	shaders/atmosphere.frag
	shaders/brdf_lut.frag
	shaders/glass.frag
	shaders/imgui.frag
	shaders/particle.vert
	shaders/particle.frag
	shaders/particles.comp
	shaders/bloom_down.frag
	shaders/bloom_up.frag
	shaders/cluster_bounds.comp
	shaders/cluster_lights.comp
	shaders/outline_flood.frag
	shaders/outline_seed.frag
	shaders/outlines.frag
	shaders/output.frag
	shaders/overlay.vert
	shaders/overlay.frag
	shaders/planet.frag
	shaders/sky.frag
	shaders/ssao.frag
	shaders/ssao_blur.frag
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
# Glass reads what the default program does: mesh.vert as it is, under the glass program's name.
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/mesh.vert OUTPUT shaders/glass.vert)
# The sun's shadow pass: the mesh vertex stage alone, into a cascade.
jettison_compile_shader_variant(jettison_assets
	SOURCE shaders/mesh.vert OUTPUT shaders/shadow_depth.vert DEFINES SHADOW_DEPTH)
# The opaque depth prepass: the mesh vertex stage into the view, with the normal and the object's ID
# for depth_prepass.frag.
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
		prefilter_irradiance ssao ssao_blur bloom_down bloom_up outline_seed outline_flood outlines)
	jettison_compile_shader_variant(jettison_assets
		SOURCE shaders/fullscreen.vert OUTPUT shaders/${program}.vert)
endforeach()

# The staged assets as one pack beside them, as a shipped build has them (assets.pack, which the
# shell mounts when there is no assets directory, or given with --assets). Not built by default:
# the smoke test builds it, and the sample's content pack, and runs from them.
jettison_add_pack(jettison_assets_pack
	SOURCE "${CMAKE_CURRENT_BINARY_DIR}"
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}.pack"
	DIRECTORIES fonts shaders strings ui
	DEPENDS jettison_assets
)
