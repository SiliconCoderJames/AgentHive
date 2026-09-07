add_executable(zworkbench WIN32
    main.cpp
    mainwindow.cpp
    app.rc
    panels/dashboard_panel.cpp
    panels/knowledge_panel.cpp
    panels/skills_panel.cpp
    panels/memory_panel.cpp
    panels/messages_panel.cpp
    panels/errors_panel.cpp
    panels/logs_panel.cpp
)
target_link_libraries(zworkbench PRIVATE zplatform_core Qt6::Widgets)
set_target_properties(zworkbench PROPERTIES AUTOMOC ON AUTORCC ON)
qt_add_resources(zworkbench "theme"
    PREFIX "/theme"
    FILES qss/dark.qss)
qt_add_resources(zworkbench "brand"
    PREFIX "/brand"
    FILES ../../docs/assets/logo.png)
