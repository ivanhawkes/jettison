# The renderer, its Vulkan backend, and for phase 1 the scene graph and its file format (see
# docs/architecture.md, "Parked code"). The Vulkan headers are public because the app shell builds
# the backend's objects itself; phase 4's renderer interface (IRenderer.h) is what takes them out
# of the game's include path.
jettison_add_module(render
	DEPENDS
		animation
		math
		profile
		reflect
		resource
		serialise
		vfs
	HEADERS
		include/jettison/render/Camera.h
		include/jettison/render/CameraRigs.h
		include/jettison/render/Clusters.h
		include/jettison/render/Culling.h
		include/jettison/render/DebugDraw.h
		include/jettison/render/DrawBatches.h
		include/jettison/render/FrameStats.h
		include/jettison/render/IRenderer.h
		include/jettison/render/Material.h
		include/jettison/render/PassList.h
		include/jettison/render/Procedural.h
		include/jettison/render/Scene.h
		include/jettison/render/SceneSystems.h
		include/jettison/render/Shadows.h
		include/jettison/render/Texture.h
		include/jettison/render/vulkan/DeviceContext.h
		include/jettison/render/vulkan/Environment.h
		include/jettison/render/vulkan/FrameData.h
		include/jettison/render/vulkan/GpuProfiler.h
		include/jettison/render/vulkan/MaterialCache.h
		include/jettison/render/vulkan/Model.h
		include/jettison/render/vulkan/PipelineCache.h
		include/jettison/render/vulkan/Renderer.h
		include/jettison/render/vulkan/Swapchain.h
		include/jettison/render/vulkan/Window.h
		include/jettison/render/vulkan/imgui/ImGuiRenderer.h
	SOURCES
		src/Camera.cpp
		src/CameraRigs.cpp
		src/Clusters.cpp
		src/Culling.cpp
		src/DebugDraw.cpp
		src/DrawBatches.cpp
		src/Material.cpp
		src/PassList.cpp
		src/Procedural.cpp
		src/Scene.cpp
		src/SceneSystems.cpp
		src/Shadows.cpp
		src/Texture.cpp
		src/vulkan/DeviceContext.cpp
		src/vulkan/Environment.cpp
		src/vulkan/GpuProfiler.cpp
		src/vulkan/MaterialCache.cpp
		src/vulkan/Memory.cpp
		src/vulkan/Model.cpp
		src/vulkan/ModelGltf.cpp
		src/vulkan/PassBarriers.cpp
		src/vulkan/PassBarriers.h
		src/vulkan/PipelineCache.cpp
		src/vulkan/Renderer.cpp
		src/vulkan/Swapchain.cpp
		src/vulkan/Window.cpp
		src/vulkan/imgui/ImGuiRenderer.cpp
	TESTS
		tests/BrdfTests.cpp
		tests/CameraRigTests.cpp
		tests/CameraTests.cpp
		tests/ClusterTests.cpp
		tests/CullingTests.cpp
		tests/DrawBatchTests.cpp
		tests/MaterialTests.cpp
		tests/PassListTests.cpp
		tests/ProceduralTests.cpp
		tests/SceneTests.cpp
		tests/ShadowTests.cpp
		tests/TextureTests.cpp
	PUBLIC_LIBRARIES
		Vulkan::Vulkan
		glm
		EnTT::EnTT
	PRIVATE_LIBRARIES
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
endif()
