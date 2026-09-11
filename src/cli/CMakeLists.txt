add_executable(agent-cli main.cpp)
target_link_libraries(agent-cli PRIVATE agenthive_core)

add_executable(platformd serverd.cpp)
target_link_libraries(platformd PRIVATE agenthive_core)

# 两个命令行程序与工作台装在同一目录：ZIP 与 MSI 都从 cmake --install 的暂存目录取材，
# 装完后 agent-cli / platformd 可直接在 PATH 上调用（MSI 会把它加入用户 PATH）。
install(TARGETS agent-cli platformd RUNTIME DESTINATION . COMPONENT Runtime)
