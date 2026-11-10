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

# World.
add_subdirectory(sim)
add_subdirectory(celestial)
add_subdirectory(spatial)
add_subdirectory(scene)
add_subdirectory(input)

# Presentation and simulation services. render depends on its sibling animation: the model loader
# fills an animation rig, and the scene systems drive the clip player.
add_subdirectory(animation)
add_subdirectory(render)
add_subdirectory(physics)
add_subdirectory(audio)
add_subdirectory(ui)
add_subdirectory(ai)
add_subdirectory(script)

# Shell.
add_subdirectory(app)
