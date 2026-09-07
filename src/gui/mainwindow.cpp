#include "mainwindow.h"

#include <QHBoxLayout>
#include <QStatusBar>

#include "gui_util.h"
#include "panels/dashboard_panel.h"
#include "panels/errors_panel.h"
#include "panels/knowledge_panel.h"
#include "panels/logs_panel.h"
#include "panels/memory_panel.h"
#include "panels/messages_panel.h"
#include "panels/skills_panel.h"
#include "theme.h"

MainWindow::MainWindow(zp::Platform& platform, QWidget* parent)
    : QMainWindow(parent), platform_(platform) {
    setWindowTitle("AgentHive · 多 Agent 协作工作台");

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- 侧边栏：品牌区 + 图标导航 + 版本脚注 ----
    auto* side = new QWidget(central);
    side->setFixedWidth(172);
    side->setStyleSheet("QWidget { background:#18181b; }");
    auto* sideLay = new QVBoxLayout(side);
    sideLay->setContentsMargins(0, 0, 0, 10);
    sideLay->setSpacing(0);

    auto* brand = new QWidget(side);
    auto* brandLay = new QVBoxLayout(brand);
    brandLay->setContentsMargins(16, 16, 12, 14);
    brandLay->setSpacing(2);
    auto* logo = new QLabel("🐝 AgentHive", brand);
    logo->setStyleSheet("font-size:16px; font-weight:800; color:#e5e5e5; background:transparent;");
    auto* tagline = new QLabel("多 Agent 协作工作台", brand);
    tagline->setStyleSheet("font-size:10px; color:#9ca3af; background:transparent;");
    brandLay->addWidget(logo);
    brandLay->addWidget(tagline);
    sideLay->addWidget(brand);

    nav_ = new QListWidget(side);
    nav_->setStyleSheet(
        "QListWidget { background:#18181b; border:none; padding:0 6px; font-size:13px; }"
        "QListWidget::item { padding:11px 12px; margin:2px 4px; border-radius:6px; color:#9ca3af; }"
        "QListWidget::item:hover { background:#262626; color:#e5e5e5; }"
        "QListWidget::item:selected { background:#0ea5e9; color:#ffffff; font-weight:600; }");
    sideLay->addWidget(nav_, 1);

    auto* ver = new QLabel("v1.0 · local-first", side);
    ver->setAlignment(Qt::AlignCenter);
    ver->setStyleSheet("font-size:10px; color:#52525b; background:transparent;");
    sideLay->addWidget(ver);
    layout->addWidget(side);

    stack_ = new QStackedWidget(central);
    layout->addWidget(stack_, 1);

    panels_ = {
        new DashboardPanel(platform_, this),
        new KnowledgePanel(platform_, this),
        new SkillsPanel(platform_, this),
        new MemoryPanel(platform_, this),
        new MessagesPanel(platform_, this),
        new ErrorsPanel(platform_, this),
        new LogsPanel(platform_, this),
    };
    for (auto* p : panels_) stack_->addWidget(p);

    setCentralWidget(central);
    buildNav();
    buildStatusBar();

    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::onNavChanged);
    nav_->setCurrentRow(0);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::onRefresh);
    timer_->start(3000);  // 3 秒自动刷新当前面板与状态栏
}

void MainWindow::buildNav() {
    const QStringList items{"📊  总览",      "📚  知识库", "🧩  技能库",
                            "🧠  用户记忆",  "💬  Agent 交流", "🚨  错误报告",
                            "🕘  操作日志"};
    nav_->addItems(items);
}

void MainWindow::buildStatusBar() {
    statusServer_ = new QLabel(this);
    statusUsage_ = new QLabel(this);
    spin_ = new QLabel(this);
    spin_->setStyleSheet(QString("color:%1; font-size:14px;").arg(ui::ACCENT.name()));
    statusBar()->addWidget(spin_);
    statusBar()->addWidget(statusServer_);
    statusBar()->addPermanentWidget(statusUsage_);
}

void MainWindow::onNavChanged(int row) {
    if (row >= 0 && row < stack_->count()) {
        stack_->setCurrentIndex(row);
        panels_[static_cast<size_t>(row)]->refresh();
    }
}

void MainWindow::onRefresh() {
    // 刷新微动画：状态栏旋转指示符
    static const QString kFrames = "◐◓◑◒";
    spin_->setText(kFrames[spinPhase_++ % 4]);
    int idx = stack_->currentIndex();
    if (idx >= 0 && idx < static_cast<int>(panels_.size())) panels_[static_cast<size_t>(idx)]->refresh();
    updateStatusBar();
}

void MainWindow::updateStatusBar() {
    std::string err;
    zp::UsageSummary sum;
    if (platform_.usageSummary(sum, err)) {
        int port = platform_.httpPort();
        statusServer_->setText(
            QString("HTTP: http://127.0.0.1:%1 · 数据: %2")
                .arg(port)
                .arg(QString::fromStdString(platform_.homeDir())));
        double pct = sum.budget > 0 ? 100.0 * sum.total_tokens / sum.budget : 0.0;
        QString color = sum.alert_level == "none" ? "#2e7d32" : (sum.alert_level == "warn" ? "#e65100" : "#c62828");
        statusUsage_->setText(
            QString("<span style='color:%1'>本周 Token: %2 / %3 (%4%) · 剩余 %5</span>")
                .arg(color)
                .arg(formatNum(sum.total_tokens))
                .arg(formatNum(sum.budget))
                .arg(pct, 0, 'f', 1)
                .arg(formatNum(sum.budget - sum.total_tokens)));
    }
}
