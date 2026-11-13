# The packer: a directory into a pack file the VFS mounts (vfs/Pack.h). A build packs with
# jettison_add_pack (source/CMakeLists.txt), which runs this.
add_executable(jettison_pack main.cpp)
set_property(TARGET jettison_pack PROPERTY FOLDER "Tools")
target_link_libraries(jettison_pack PRIVATE Jettison::Vfs Jettison::Core jettison_warnings)
