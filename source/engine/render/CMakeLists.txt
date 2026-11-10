# The renderer, its Vulkan backend, and for phase 1 the scene graph and its file format (see
# docs/architecture.md, "Parked code"). The Vulkan headers are public because the app shell builds
# the backend's objects itself; phase 4's renderer interface (IRenderer.h) is what takes them out
# of the game's include path.
jettison_add_module(render
	DEPENDS
		animation
	HEADERS
		include/jettison/render/IRenderer.h
		include/jettison/render/Scene.h
		include/jettison/render/SceneSystems.h
		include/jettison/render/vulkan/DeviceContext.h
		include/jettison/render/vulkan/Model.h
		include/jettison/render/vulkan/Pipeline.h
		include/jettison/render/vulkan/Renderer.h
		include/jettison/render/vulkan/Swapchain.h
		include/jettison/render/vulkan/Window.h
		include/jettison/render/vulkan/imgui/ImGuiRenderer.h
	SOURCES
		src/Scene.cpp
		src/SceneSystems.cpp
		src/vulkan/DeviceContext.cpp
		src/vulkan/Model.cpp
		src/vulkan/ModelGltf.cpp
		src/vulkan/Pipeline.cpp
		src/vulkan/Renderer.cpp
		src/vulkan/Swapchain.cpp
		src/vulkan/Window.cpp
		src/vulkan/imgui/ImGuiRenderer.cpp
	TESTS
		tests/SceneTests.cpp
	PUBLIC_LIBRARIES
		Vulkan::Vulkan
		glm
		EnTT::EnTT
	PRIVATE_LIBRARIES
		glfw
		stb
		yaml-cpp::yaml-cpp
		fastgltf
		"$<LINK_GROUP:RESCAN,imgui>"
)
