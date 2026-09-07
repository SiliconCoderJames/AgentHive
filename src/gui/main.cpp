#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>

#include <cstdlib>

#include "core/platform.h"
#include "mainwindow.h"

// 程序化绘制蜂巢图标：深色圆角底 + 琥珀色六边形蜂巢 + 入口点
static QPixmap hiveIcon(int side) {
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    qreal m = side * 0.08, w = side - 2 * m;
    QRectF box(m, m, w, w);
    p.setBrush(QColor("#1e1e1e"));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(box, side * 0.2, side * 0.2);
    auto hex = [](QPainter& pp, const QPointF& c, qreal r) {
        QPolygonF h;
        for (int i = 0; i < 6; ++i) {
            qreal a = M_PI / 180.0 * (60 * i - 30);
            h << c + QPointF(r * std::cos(a), r * std::sin(a));
        }
        pp.drawPolygon(h);
    };
    QPen pen(QColor("#f59e0b"), side * 0.055, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    qreal r = w * 0.21;
    hex(p, QPointF(side / 2.0, side / 2.0 - r * 1.02), r);
    hex(p, QPointF(side / 2.0 - r * 0.9, side / 2.0 + r * 0.55), r);
    hex(p, QPointF(side / 2.0 + r * 0.9, side / 2.0 + r * 0.55), r);
    p.setBrush(QColor("#0ea5e9"));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(side / 2.0, side / 2.0 - r * 1.02), r * 0.32, r * 0.32);
    return pm;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("AgentHive 多 Agent 协作工作台");
    app.setOrganizationName("agenthive");
    {
        // 优先使用仓库品牌图标（与 README 一致），缺失时回退到程序化绘制的蜂巢
        QIcon brandIcon(":/brand/logo.png");
        if (!brandIcon.isNull())
            app.setWindowIcon(brandIcon);
        else
            app.setWindowIcon(QIcon(hiveIcon(64)));
    }

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
