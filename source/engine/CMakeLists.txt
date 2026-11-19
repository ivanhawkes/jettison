# The engine modules, lowest tier first: jettison_add_module refuses a dependency on a module that
# has not been added yet, so this order is the dependency graph (docs/architecture.md).

# Foundation.
add_subdirectory(core)
add_subdirectory(math)

# Services.
add_subdirectory(platform)
add_subdirectory(profile)
add_subdirectory(time)
add_subdirectory(jobs)
add_subdirectory(reflect)
add_subdirectory(vfs)

# Data.
add_subdirectory(serialise)
add_subdirectory(resource)
add_subdirectory(ecs)

# World. animation is pure CPU, and the scene's systems drive its clip players, so it comes before
# the scene.
add_subdirectory(animation)
add_subdirectory(sim)
add_subdirectory(celestial)
add_subdirectory(spatial)
add_subdirectory(scene)
add_subdirectory(input)

# Simulation services.
add_subdirectory(physics)
add_subdirectory(ai)
# The space capability over them, which no neutral module depends on.
add_subdirectory(space)
add_subdirectory(spacescene)
add_subdirectory(dock)
add_subdirectory(script)

# The CPU session a game starts through.
add_subdirectory(host)

# The network transport a game's server and clients are built on.
add_subdirectory(net)

# Presentation, and the render shell on it.
if(JETTISON_PRESENTATION)
	add_subdirectory(render)
	add_subdirectory(audio)
	add_subdirectory(ui)
	add_subdirectory(ui/imgui)
	add_subdirectory(app)
endif()
