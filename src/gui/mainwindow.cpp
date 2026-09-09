#include "mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QShortcut>
#include <QStatusBar>

#include <utility>

#include "gui_util.h"
#include "panels/dashboard_panel.h"
#include "panels/errors_panel.h"
#include "panels/knowledge_panel.h"
#include "panels/logs_panel.h"
#include "panels/memory_panel.h"
#include "panels/messages_panel.h"
#include "panels/skills_panel.h"
#include "settings_dialog.h"
#include "theme.h"

MainWindow::MainWindow(zp::Platform& platform, QWidget* parent)
    : QMainWindow(parent), platform_(platform) {
    setWindowTitle("AgentHive · 多 Agent 协作工作台");

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- 侧边栏：品牌区 + 图标导航 + 版本脚注（样式统一在 applyChrome，随主题重涂）----
    side_ = new QWidget(central);
    side_->setFixedWidth(172);
    auto* sideLay = new QVBoxLayout(side_);
    sideLay->setContentsMargins(0, 0, 0, 10);
    sideLay->setSpacing(0);

    auto* brand = new QWidget(side_);
    auto* brandLay = new QVBoxLayout(brand);
    brandLay->setContentsMargins(16, 16, 12, 14);
    brandLay->setSpacing(2);
    logo_ = new QLabel("🐝 AgentHive", brand);
    tagline_ = new QLabel(brand);
    brandLay->addWidget(logo_);
    brandLay->addWidget(tagline_);
    sideLay->addWidget(brand);

    // 品牌分隔线：品牌色→强调色横向渐变，蜂巢品牌签名
    brandLine_ = new QFrame(side_);
    brandLine_->setFixedHeight(2);
    sideLay->addWidget(brandLine_);

    nav_ = new QListWidget(side_);
    nav_->setFocusPolicy(Qt::NoFocus);  // 去除选中项虚线焦点框；Ctrl+1..7 仍可切换面板
    sideLay->addWidget(nav_, 1);

    // 脚注：版本号 + 主题/字号切换 + 语言切换（持久化到 QSettings）
    auto* foot = new QWidget(side_);
    auto* footLay = new QHBoxLayout(foot);
    footLay->setContentsMargins(10, 0, 10, 0);
    ver_ = new QLabel("v1.0 · local-first", foot);
    themeBtn_ = new QToolButton(foot);
    themeBtn_->setText("🎨");
    themeBtn_->setToolTip(i18n::trs("主题与字号", "Theme & font size"));
    themeBtn_->setPopupMode(QToolButton::InstantPopup);
    auto* themeMenu = new QMenu(themeBtn_);
    auto* themeGroup = new QActionGroup(themeMenu);
    themeGroup->setExclusive(true);
    for (int i = 0; i < static_cast<int>(ui::themes().size()); ++i) {
        const auto& t = ui::themes()[static_cast<size_t>(i)];
        auto* act = themeMenu->addAction(i18n::trs(t.zh, t.en));
        act->setCheckable(true);
        act->setChecked(i == ui::themeIdx());
        themeGroup->addAction(act);
        connect(act, &QAction::triggered, this, [i] { ui::setThemeIndex(i); });
    }
    themeMenu->addSeparator();
    auto* fontGroup = new QActionGroup(themeMenu);
    fontGroup->setExclusive(true);
    const std::vector<std::pair<int, const char*>> fontSizes{
        {12, "紧凑 12px"}, {13, "标准 13px"}, {14, "大号 14px"}};
    for (const auto& [px, label] : fontSizes) {
        auto* act = themeMenu->addAction(QString::fromUtf8(label));
        act->setCheckable(true);
        act->setChecked(ui::fontBaseRef() == px);
        fontGroup->addAction(act);
        connect(act, &QAction::triggered, this, [px] { ui::setFontBase(px); });
    }
    themeBtn_->setMenu(themeMenu);
    settingsBtn_ = new QToolButton(foot);
    settingsBtn_->setText("⚙");
    settingsBtn_->setToolTip(i18n::trs("设置", "Settings"));
    connect(settingsBtn_, &QToolButton::clicked, this, &MainWindow::openSettings);
    langBtn_ = new QToolButton(foot);
    connect(langBtn_, &QToolButton::clicked, this, [] { i18n::toggle(); });
    footLay->addWidget(ver_);
    footLay->addStretch(1);
    footLay->addWidget(themeBtn_);
    footLay->addWidget(settingsBtn_);
    footLay->addWidget(langBtn_);
    sideLay->addWidget(foot);
    layout->addWidget(side_);

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
    applyChrome();
    applyLanguage();

    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::onNavChanged);
    nav_->setCurrentRow(0);

    // 语言切换：重译铬层（导航/页头/状态栏），面板各自处理自有文案
    i18n::listeners().push_back([this] { applyLanguage(); });
    // 主题/字号切换：重生成 QSS、重涂铬层、重建面板
    ui::themeListeners().push_back([this] { applyTheme(); });
    // 界面偏好（刷新频率/错误提醒）由设置对话框写入后经同一通知重读
    ui::themeListeners().push_back([this] { applyUiPrefs(); });

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::onRefresh);
    applyUiPrefs();  // 读取刷新频率（默认 3s）与错误提醒偏好
    timer_->start(timer_->interval());

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
    nav_->setStyleSheet(ui::th(
        "QListWidget { background:@deep@; border:none; padding:0 6px; font-size:13px; }"
        "QListWidget::item { padding:11px 12px; margin:2px 4px; border-radius:6px;"
        " color:@muted@; border-left:3px solid transparent; }"
        "QListWidget::item:hover { background:@card@; color:@text@; }"
        "QListWidget::item:selected { background:@selbg@; color:@seltext@;"
        " font-weight:600; border-left:3px solid @brand@; }"));
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
    nav_->item(5)->setForeground(QBrush(ui::danger()));  // 错误报告项恒红，异常时更醒目
}

void MainWindow::applyLanguage() {
    setWindowTitle(i18n::trs("AgentHive · 多 Agent 协作工作台",
                             "AgentHive · Multi-Agent Collaboration Workbench"));
    tagline_->setText(i18n::trs("多 Agent 协作工作台", "multi-agent collaboration hub"));
    langBtn_->setText(i18n::g_lang == i18n::Lang::Zh ? "EN" : "中文");
    langBtn_->setToolTip(i18n::trs("切换语言", "Switch language"));
    settingsBtn_->setToolTip(i18n::trs("设置", "Settings"));
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

void MainWindow::applyChrome() {
    side_->setStyleSheet(ui::th("QWidget { background:@deep@; }"));
    logo_->setStyleSheet(
        ui::th("font-size:16px; font-weight:800; color:@text@; background:transparent;"));
    tagline_->setStyleSheet(ui::th("font-size:10px; color:@muted@; background:transparent;"));
    brandLine_->setStyleSheet(ui::th(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 @brand@, stop:0.55 @accent@, stop:1 transparent);"
        "border-radius:1px; margin:0 16px 8px 16px;"));
    ver_->setStyleSheet(ui::th("font-size:10px; color:@muted@; background:transparent;"));
    const QString btn = ui::th(
        "QToolButton { color:@muted@; font-size:10px; border:1px solid @line@;"
        " border-radius:6px; padding:2px 8px; }"
        "QToolButton:hover { color:@text@; border-color:@accent@; }");
    langBtn_->setStyleSheet(btn);
    themeBtn_->setStyleSheet(btn);
    settingsBtn_->setStyleSheet(btn);
}

void MainWindow::applyUiPrefs() {
    QSettings s;
    int ms = s.value("ui/refreshMs", 3000).toInt();
    if (ms < 1000 || ms > 60000) ms = 3000;
    timer_->setInterval(ms);
    errorToast_ = s.value("ui/errorToast", false).toBool();
}

void MainWindow::openSettings() {
    if (!settings_) {
        settings_ = new SettingsDialog(platform_, this);
        settings_->setAttribute(Qt::WA_DeleteOnClose);
    }
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
}

void MainWindow::applyTheme() {
    qApp->setStyleSheet(ui::themeQss());  // 全局 QSS 随主题重生成
    applyChrome();
    int row = nav_->currentRow();
    buildNav();
    if (row >= 0) nav_->setCurrentRow(row);
    rebuildPanels();  // 面板样式在构造时取色，整体重建
    updateStatusBar();
}

void MainWindow::buildStatusBar() {
    statusServer_ = new QLabel(this);
    statusUsage_ = new QLabel(this);
    spin_ = new QLabel(this);
    spin_->setStyleSheet(ui::th("color:@accent@; font-size:14px;"));
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
    statusServer_->setText(QString("<span style='color:%1;'>●</span> HTTP: "
                                   "http://127.0.0.1:%2 · %3 %4")
                               .arg(ui::ok().name())
                               .arg(platform_.httpPort())
                               .arg(i18n::trs("数据", "data"))
                               .arg(QString::fromStdString(platform_.homeDir())));
    if (platform_.usageSummary(sum, err)) {
        double pct = sum.budget > 0 ? 100.0 * sum.total_tokens / sum.budget : 0.0;
        QString color = sum.alert_level == "none" ? ui::ok().name()
                        : (sum.alert_level == "warn" ? ui::warn().name() : ui::danger().name());
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
        // 偏好开启时，新增未解决错误弹提醒（首次采样不提醒）
        if (errorToast_ && seenOpenErrors_ >= 0 && openErrors_ > seenOpenErrors_) {
            ui::Toast::show(this, i18n::trs("⚠ 新增 %1 条未解决错误", "⚠ %1 new unresolved "
                                            "error(s)").arg(openErrors_ - seenOpenErrors_),
                            false);
        }
        seenOpenErrors_ = openErrors_;
        nav_->item(5)->setText(
            openErrors_ > 0
                ? QString("🚨  %1  (%2)").arg(i18n::trs("错误报告", "Errors")).arg(openErrors_)
                : "🚨  " + i18n::trs("错误报告", "Errors"));
    }
}
