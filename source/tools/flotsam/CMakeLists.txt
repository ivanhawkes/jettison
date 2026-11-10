add_executable(flotsam main.cpp)
jettison_use_shared_assets(flotsam)
set_property(TARGET flotsam PROPERTY FOLDER "Flotsam")
target_link_libraries(flotsam PRIVATE Jettison::App jettison_warnings)
