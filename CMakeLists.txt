cmake_minimum_required (VERSION 3.17)

# Set the overall project name.
project("Jettison" VERSION 0.1.0.0)

# Turn on using solution folders.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Source.
add_subdirectory("source")
