# The engine modules, lowest tier first: jettison_add_module refuses a dependency on a module that
# has not been added yet, so this order is the dependency graph (docs/architecture.md).

# Foundation.
add_subdirectory(core)

# Presentation and simulation services. render depends on its sibling animation: the model loader
# fills an animation rig, and the scene systems drive the clip player.
add_subdirectory(animation)
add_subdirectory(render)

# Shell.
add_subdirectory(app)
