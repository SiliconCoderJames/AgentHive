add_executable(zworkbench WIN32
    main.cpp
    mainwindow.cpp
    panels/dashboard_panel.cpp
    panels/knowledge_panel.cpp
    panels/skills_panel.cpp
    panels/memory_panel.cpp
    panels/messages_panel.cpp
    panels/errors_panel.cpp
    panels/logs_panel.cpp
)
target_link_libraries(zworkbench PRIVATE zplatform_core Qt6::Widgets)
set_target_properties(zworkbench PROPERTIES AUTOMOC ON)

# 构建后自动部署 Qt 运行库（DLL 与 exe 同目录），保证双击即可启动。
# windeployqt 从 Qt6_DIR 反推；未找到则跳过（可手动运行 windeployqt）。
if(WIN32)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET zworkbench POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                    "$<IF:$<CONFIG:Debug>,--debug,--release>"
                    --no-opengl-sw --no-system-d3d-compiler --no-translations
                    "$<TARGET_FILE:zworkbench>"
            COMMENT "Deploying Qt runtime next to zworkbench")
    endif()
endif()
