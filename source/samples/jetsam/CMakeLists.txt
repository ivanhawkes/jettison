add_executable(jetsam main.cpp)
jettison_use_shared_assets(jetsam)
set_property(TARGET jetsam PROPERTY FOLDER "Jetsam")
target_link_libraries(jetsam PRIVATE Jettison::App jettison_warnings)
