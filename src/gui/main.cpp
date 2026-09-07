#include <QApplication>
#include <QFile>
#include <QMessageBox>

#include <cstdlib>

#include "core/platform.h"
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("AgentHive 多 Agent 协作工作台");
    app.setOrganizationName("agenthive");

    // 强制深色主题：QSS 统一管理（背景 #1e1e1e / 卡片 #2d2d2d / 强调 #0ea5e9）
    // 注意 qt_add_resources(PREFIX "/theme") 会把子目录 qss/ 拼进资源路径
    QFile qss(":/theme/qss/dark.qss");
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray body = qss.readAll();
        app.setStyleSheet(QString::fromUtf8(body));
    } else {
        qWarning("dark.qss open FAILED: %s", qss.errorString().toUtf8().constData());
    }

    zp::Platform platform(zp::defaultHomeDir());
    std::string err;
    if (!platform.bootstrap(err)) {
        QMessageBox::critical(nullptr, "AgentHive 工作台",
                              QString::fromStdString("平台初始化失败: " + err));
        return 1;
    }

    // 内置 HTTP 服务供 Agent 接入（仅绑定 127.0.0.1）
    int port = 8787;
    if (const char* env = std::getenv("ZCODE_PLATFORM_PORT"); env && *env)
        port = std::atoi(env);
    std::string serr;
    if (!platform.startHttpServer(port, serr)) {
        QMessageBox::warning(nullptr, "AgentHive 工作台",
                             QString::fromStdString("HTTP 服务启动失败，Agent 将无法接入: " + serr));
    }

    MainWindow w(platform);
    w.resize(1280, 800);
    w.setMinimumSize(1080, 680);
    w.show();
    return app.exec();
}
