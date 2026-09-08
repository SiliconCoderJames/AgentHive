#include "mainwindow.h"

#include <QHBoxLayout>
#include <QShortcut>
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
    tagline_ = new QLabel(brand);
    tagline_->setStyleSheet("font-size:10px; color:#9ca3af; background:transparent;");
    brandLay->addWidget(logo);
    brandLay->addWidget(tagline_);
    sideLay->addWidget(brand);

    nav_ = new QListWidget(side);
    nav_->setStyleSheet(
        "QListWidget { background:#18181b; border:none; padding:0 6px; font-size:13px; }"
        "QListWidget::item { padding:11px 12px; margin:2px 4px; border-radius:6px;"
        " color:#9ca3af; border-left:3px solid transparent; }"
        "QListWidget::item:hover { background:#262626; color:#e5e5e5; }"
        "QListWidget::item:selected { background:#0ea5e9; color:#ffffff;"
        " font-weight:600; border-left:3px solid #f59e0b; }");
    sideLay->addWidget(nav_, 1);

    // 脚注：版本号 + 语言切换（持久化到 QSettings）
    auto* foot = new QWidget(side);
    auto* footLay = new QHBoxLayout(foot);
    footLay->setContentsMargins(10, 0, 10, 0);
    auto* ver = new QLabel("v1.0 · local-first", foot);
    ver->setStyleSheet("font-size:10px; color:#52525b; background:transparent;");
    langBtn_ = new QToolButton(foot);
    langBtn_->setStyleSheet(
        "QToolButton { color:#9ca3af; font-size:10px; border:1px solid #3f3f46;"
        " border-radius:6px; padding:2px 8px; }"
        "QToolButton:hover { color:#e5e5e5; border-color:#0ea5e9; }");
    connect(langBtn_, &QToolButton::clicked, this, [] { i18n::toggle(); });
    footLay->addWidget(ver);
    footLay->addStretch(1);
    footLay->addWidget(langBtn_);
    sideLay->addWidget(foot);
    layout->addWidget(side);

    stack_ = new QStackedWidget(central);
    layout->addWidget(stack_, 1);

    panelFactories_ = {
        [](zp::Platform& pl, QWidget* parent) { return static_cast<PanelBase*>(new DashboardPanel(pl, parent)); },
        [](zp::Platform& pl, QWidget* parent) { return static_cast<PanelBase*>(new KnowledgePanel(pl, parent)); },
        [](zp::Platform& pl, QWidget* parent) { return static_cast<PanelBase*>(new SkillsPanel(pl, parent)); },
        [](zp::Platform& pl, QWidget* parent) { return static_cast<PanelBase*>(new MemoryPanel(pl, parent)); },
        [](zp::Platform& pl, QWidget* parent) { return static_cast<PanelBase*>(new MessagesPanel(pl, parent)); },
        [](zp::Platform& pl, QWidget* parent) { return static_cast<PanelBase*>(new ErrorsPanel(pl, parent)); },
        [](zp::Platform& pl, QWidget* parent) { return static_cast<PanelBase*>(new LogsPanel(pl, parent)); },
    };
    rebuildPanels();

    setCentralWidget(central);
    buildNav();
    buildStatusBar();
    applyLanguage();

    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::onNavChanged);
    nav_->setCurrentRow(0);

    // 语言切换：重译铬层（导航/页头/状态栏），面板各自处理自有文案
    i18n::listeners().push_back([this] { applyLanguage(); });

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::onRefresh);
    timer_->start(3000);  // 3 秒自动刷新当前面板与状态栏

    // 快捷键：Ctrl+1..7 切面板，F5 手动刷新
    for (int i = 0; i < 7; ++i) {
        auto* sc = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i + 1)), this);
        connect(sc, &QShortcut::activated, this, [this, i] { nav_->setCurrentRow(i); });
    }
    auto* refreshSc = new QShortcut(QKeySequence("F5"), this);
    connect(refreshSc, &QShortcut::activated, this, &MainWindow::onRefresh);
}

void MainWindow::buildNav() {
    nav_->clear();
    const QStringList items{
        "📊  " + i18n::trs("总览", "Overview"),
        "📚  " + i18n::trs("知识库", "Knowledge"),
        "🧩  " + i18n::trs("技能库", "Skills"),
        "🧠  " + i18n::trs("用户记忆", "Memory"),
        "💬  " + i18n::trs("Agent 交流", "Messaging"),
        "🚨  " + i18n::trs("错误报告", "Errors"),
        "🕘  " + i18n::trs("操作日志", "Audit"),
    };
    nav_->addItems(items);
    nav_->item(5)->setForeground(QBrush(ui::DANGER));  // 错误报告项恒红，异常时更醒目
}

void MainWindow::applyLanguage() {
    setWindowTitle(i18n::trs("AgentHive · 多 Agent 协作工作台",
                             "AgentHive · Multi-Agent Collaboration Workbench"));
    tagline_->setText(i18n::trs("多 Agent 协作工作台", "multi-agent collaboration hub"));
    langBtn_->setText(i18n::g_lang == i18n::Lang::Zh ? "EN" : "中文");
    langBtn_->setToolTip(i18n::trs("切换语言", "Switch language"));
    int row = nav_->currentRow();
    buildNav();
    if (row >= 0) nav_->setCurrentRow(row);
    rebuildPanels();  // 面板整体重建：ctor 里的 trs() 随新语言重新求值
    updateStatusBar();
}

void MainWindow::rebuildPanels() {
    int row = nav_ ? nav_->currentRow() : 0;
    for (auto* p : panels_) {
        stack_->removeWidget(p);
        p->deleteLater();
    }
    panels_.clear();
    for (auto& f : panelFactories_) {
        panels_.push_back(f(platform_, this));
        stack_->addWidget(panels_.back());
    }
    if (row >= 0 && row < static_cast<int>(panels_.size())) {
        stack_->setCurrentIndex(row);
        panels_[static_cast<size_t>(row)]->refresh();
    }
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
    // 服务在线指示灯：HTTP 服务随进程常驻，绿点常亮即后端可用
    statusServer_->setText(QString("<span style='color:#22c55e;'>●</span> HTTP: "
                                   "http://127.0.0.1:%1 · %2 %3")
                               .arg(platform_.httpPort())
                               .arg(i18n::trs("数据", "data"))
                               .arg(QString::fromStdString(platform_.homeDir())));
    if (platform_.usageSummary(sum, err)) {
        double pct = sum.budget > 0 ? 100.0 * sum.total_tokens / sum.budget : 0.0;
        QString color = sum.alert_level == "none" ? "#22c55e" : (sum.alert_level == "warn" ? "#f59e0b" : "#ef4444");
        statusUsage_->setText(
            QString("<span style='color:%1'>%2: %3 / %4 (%5%) · %6 %7</span>")
                .arg(color)
                .arg(i18n::trs("本周 Token", "Weekly tokens"))
                .arg(formatNum(sum.total_tokens))
                .arg(formatNum(sum.budget))
                .arg(pct, 0, 'f', 1)
                .arg(i18n::trs("剩余", "left"))
                .arg(formatNum(sum.budget - sum.total_tokens)));
    }
    // 导航徽标：未解决错误数附加在「错误报告」项上
    std::vector<zp::ErrorReport> openErrors;
    if (platform_.errorList("open", "", 99, openErrors, err)) {
        openErrors_ = static_cast<int>(openErrors.size());
        nav_->item(5)->setText(
            openErrors_ > 0
                ? QString("🚨  %1  (%2)").arg(i18n::trs("错误报告", "Errors")).arg(openErrors_)
                : "🚨  " + i18n::trs("错误报告", "Errors"));
    }
}
