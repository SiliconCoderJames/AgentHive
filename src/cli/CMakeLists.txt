add_executable(agent-cli main.cpp)
target_link_libraries(agent-cli PRIVATE zplatform_core)

add_executable(platformd serverd.cpp)
target_link_libraries(platformd PRIVATE zplatform_core)
