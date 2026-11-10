# The engine modules, lowest tier first: jettison_add_module refuses a dependency on a module that
# has not been added yet, so this order is the dependency graph (docs/architecture.md).

# Foundation.
add_subdirectory(core)
