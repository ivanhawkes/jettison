# The renderer: its interface (IRenderer.h), the frame's CPU side, and the Vulkan backend behind
# it. The backend's headers are private (src/vulkan), and Vulkan is a private library, so no Vulkan
# type reaches a module that links render. It draws a scene (engine/scene) by what the scene names,
# loading the models through the resource manager.
jettison_add_module(render
	DEPENDS
		animation
		math
		profile
		reflect
		resource
		scene
		serialise
		vfs
	HEADERS
		include/jettison/render/Atmosphere.h
		include/jettison/render/Camera.h
		include/jettison/render/Clusters.h
		include/jettison/render/Culling.h
		include/jettison/render/DebugDraw.h
		include/jettison/render/DrawBatches.h
		include/jettison/render/FrameData.h
		include/jettison/render/FrameStats.h
		include/jettison/render/IRenderer.h
		include/jettison/render/Material.h
		include/jettison/render/Mesh.h
		include/jettison/render/Overlay.h
		include/jettison/render/PassList.h
		include/jettison/render/Procedural.h
		include/jettison/render/SceneView.h
		include/jettison/render/Screenshot.h
		include/jettison/render/Shadows.h
		include/jettison/render/Texture.h
	SOURCES
		src/Atmosphere.cpp
		src/Camera.cpp
		src/Clusters.cpp
		src/Culling.cpp
		src/DebugDraw.cpp
		src/DrawBatches.cpp
		src/Material.cpp
		src/Overlay.cpp
		src/PassList.cpp
		src/Procedural.cpp
		src/SceneView.cpp
		src/Screenshot.cpp
		src/Shadows.cpp
		src/Texture.cpp
		src/vulkan/DeviceChoice.cpp
		src/vulkan/DeviceChoice.h
		src/vulkan/DeviceContext.cpp
		src/vulkan/DeviceContext.h
		src/vulkan/Environment.cpp
		src/vulkan/Environment.h
		src/vulkan/GpuProfiler.cpp
		src/vulkan/GpuProfiler.h
		src/vulkan/MaterialCache.cpp
		src/vulkan/MaterialCache.h
		src/vulkan/Memory.cpp
		src/vulkan/Model.cpp
		src/vulkan/Model.h
		src/vulkan/ModelCache.cpp
		src/vulkan/ModelCache.h
		src/vulkan/ModelGltf.cpp
		src/vulkan/OverlayRenderer.cpp
		src/vulkan/OverlayRenderer.h
		src/vulkan/PassBarriers.cpp
		src/vulkan/PassBarriers.h
		src/vulkan/PipelineCache.cpp
		src/vulkan/PipelineCache.h
		src/vulkan/Renderer.cpp
		src/vulkan/Renderer.h
		src/vulkan/SceneDraws.cpp
		src/vulkan/SceneDraws.h
		src/vulkan/Swapchain.cpp
		src/vulkan/Swapchain.h
		src/vulkan/TextureUpload.cpp
		src/vulkan/TextureUpload.h
		src/vulkan/VulkanRenderer.cpp
		src/vulkan/Window.cpp
		src/vulkan/Window.h
		src/vulkan/imgui/ImGuiRenderer.cpp
		src/vulkan/imgui/ImGuiRenderer.h
	TESTS
		tests/AtmosphereTests.cpp
		tests/BrdfTests.cpp
		tests/CameraTests.cpp
		tests/ClusterTests.cpp
		tests/CullingTests.cpp
		tests/DebugDrawTests.cpp
		tests/DeviceChoiceTests.cpp
		tests/DrawBatchTests.cpp
		tests/EnvironmentTests.cpp
		tests/MaterialTests.cpp
		tests/OverlayTests.cpp
		tests/PassListTests.cpp
		tests/ProceduralTests.cpp
		tests/ScreenshotTests.cpp
		tests/ShadowTests.cpp
		tests/TextureTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
	PRIVATE_LIBRARIES
		Vulkan::Vulkan
		glfw
		ktx_read
		stb
		yaml-cpp::yaml-cpp
		fastgltf
		GPUOpen::VulkanMemoryAllocator
		"$<LINK_GROUP:RESCAN,imgui>"
)

# The cluster grid's functions are clusters.glsl compiled as C++ (Clusters.cpp), so the CPU
# reference runs the source the cluster programs do; the shadow cascades' selection and lookup are
# shadows.glsl, so the fit is checked against the source the lighting include runs (Shadows.cpp).
target_include_directories(jettison_render PRIVATE "${PROJECT_SOURCE_DIR}/assets/shaders/include")

# The texture and material tests read the files the assets ship (the sample KTX 2 texture, the
# program descriptors and materials), not copies of them; the BRDF tests compile the shaders' own
# lighting includes as C++.
if(TARGET jettison_render_tests)
	target_compile_definitions(jettison_render_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets")
	target_include_directories(jettison_render_tests PRIVATE
		"${PROJECT_SOURCE_DIR}/assets/shaders/include")
	# The pass list's barriers are tested through the backend's private header (vulkan/PassBarriers.h),
	# so the tests see the Vulkan the module keeps private.
	# The screenshot test reads back the PNG it wrote through stb_image, which Texture.cpp compiles.
	target_link_libraries(jettison_render_tests PRIVATE Vulkan::Vulkan stb)
endif()
