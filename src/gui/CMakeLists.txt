add_executable(zworkbench WIN32
    main.cpp
    mainwindow.cpp
    settings_dialog.cpp
    welcome_dialog.cpp
    app.rc
    panels/dashboard_panel.cpp
    panels/knowledge_panel.cpp
    panels/skills_panel.cpp
    panels/memory_panel.cpp
    panels/messages_panel.cpp
    panels/errors_panel.cpp
    panels/logs_panel.cpp
)
target_link_libraries(zworkbench PRIVATE zplatform_core Qt6::Widgets Qt6::Network Qt6::Svg)
set_target_properties(zworkbench PROPERTIES AUTOMOC ON AUTORCC ON)
qt_add_resources(zworkbench "theme"
    PREFIX "/theme"
    FILES qss/dark.qss)
qt_add_resources(zworkbench "brand"
    PREFIX "/brand"
    FILES ../../docs/assets/logo.png)

# ---- 可复现的 Qt 运行时部署 ----
# 此前项目里没有任何 windeployqt 调用，Qt6*.dll 与 platforms/tls/... 插件是人工拷进
# 构建目录的：换台机器或清空 build 就装不出能跑的包。
# 现在 `cmake --install <build> --config Release --prefix <stage>` 产出扁平且可直接运行的
# 目录（exe + Qt DLL + 插件目录 + MSVC 运行库同层），ZIP 与 MSI 都从它取材。
# 不用 qt_generate_deploy_app_script 的原因：它按 Qt 的安装布局输出 bin/ 与 plugins/
# 多级结构，还会带上 include/ 与 lib/cmake 等开发文件，不适合直接打包分发。
if(WIN32)
    install(TARGETS zworkbench RUNTIME DESTINATION . COMPONENT Runtime)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin}" REQUIRED)
    # --compiler-runtime : MSVC 运行库按"应用本地部署"随包分发，目标机无需预装 VC++ Redist
    # --no-translations  : 界面文案走自研 i18n，不用 Qt .qm
    # --no-opengl-sw/--no-system-d3d-compiler : Widgets 光栅渲染用不到，可省近 20MB
    install(CODE "
        message(STATUS \"windeployqt -> \${CMAKE_INSTALL_PREFIX}\")
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                    --release --compiler-runtime --no-translations
                    --no-opengl-sw --no-system-d3d-compiler
                    --dir \"\${CMAKE_INSTALL_PREFIX}\"
                    \"\${CMAKE_INSTALL_PREFIX}/zworkbench.exe\"
            RESULT_VARIABLE _wdq_rc)
        if(NOT _wdq_rc EQUAL 0)
            message(FATAL_ERROR \"windeployqt 失败，退出码 \${_wdq_rc}\")
        endif()
        # windeployqt --compiler-runtime 只在能找到 VC 可再分发文件时才拷贝；
        # 这里补一次兜底：从 VCToolsRedistDir 取，取不到就明确提示（发行流水线会据此报错）
        if(NOT EXISTS \"\${CMAKE_INSTALL_PREFIX}/vcruntime140.dll\"
           AND DEFINED ENV{VCToolsRedistDir})
            file(GLOB _crt
                 \"\$ENV{VCToolsRedistDir}/x64/Microsoft.VC*.CRT/*.dll\")
            foreach(_f \${_crt})
                file(COPY \"\${_f}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
            endforeach()
        endif()
        if(NOT EXISTS \"\${CMAKE_INSTALL_PREFIX}/vcruntime140.dll\")
            message(WARNING
                \"未随包带上 MSVC 运行库（vcruntime140.dll）：目标机需要预装 VC++ Redistributable，\"
                \"或设置 VCToolsRedistDir 后重新 install。发行打包请确保该文件存在。\")
        endif()
    " COMPONENT Runtime)
endif()

# 第三方许可与通知随包分发（Qt 为 LGPLv3，二进制分发必须随附许可文本）
install(FILES
    ${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md
    ${CMAKE_SOURCE_DIR}/LICENSE
    DESTINATION licenses
    COMPONENT Runtime)
