#include <QApplication>
#include <QMessageBox>

#include <cstdlib>

#include "core/platform.h"
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("ZCode 多 Agent 协作工作台");
    app.setOrganizationName("zcode");

    zp::Platform platform(zp::defaultHomeDir());
    std::string err;
    if (!platform.bootstrap(err)) {
        QMessageBox::critical(nullptr, "ZCode 工作台",
                              QString::fromStdString("平台初始化失败: " + err));
        return 1;
    }

    // 内置 HTTP 服务供 Agent 接入（仅绑定 127.0.0.1）
    int port = 8787;
    if (const char* env = std::getenv("ZCODE_PLATFORM_PORT"); env && *env)
        port = std::atoi(env);
    std::string serr;
    if (!platform.startHttpServer(port, serr)) {
        QMessageBox::warning(nullptr, "ZCode 工作台",
                             QString::fromStdString("HTTP 服务启动失败，Agent 将无法接入: " + serr));
    }

    MainWindow w(platform);
    w.resize(1280, 800);
    w.show();
    return app.exec();
}
