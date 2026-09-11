#include "settings_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QShowEvent>

#include <iterator>

#include "gui_util.h"
#include "i18n.h"
#include "theme.h"
#include "widgets.h"

namespace {
constexpr const char* kRepoApi =
    "https://api.github.com/repos/SiliconCoderJames/AgentHive/releases/latest";
constexpr const char* kRepoPage = "https://github.com/SiliconCoderJames/AgentHive/releases";

// 设置导航项：kind 取自 ui::makeIcon 的图标种类
constexpr const char* kNavKinds[] = {"palette",  "database", "bell", "overview",
                                     "plug",     "refresh",  "info"};

// 版本比较（形如 0.1.0）：a > b 返回 1，相等 0，否则 -1
int cmpVersion(const QString& a, const QString& b) {
    const QStringList pa = a.split('.');
    const QStringList pb = b.split('.');
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        int x = i < pa.size() ? pa[i].toInt() : 0;
        int y = i < pb.size() ? pb[i].toInt() : 0;
        if (x != y) return x > y ? 1 : -1;
    }
    return 0;
}
}  // namespace

SettingsDialog::SettingsDialog(zp::Platform& platform, QWidget* parent)
    : QDialog(parent), platform_(platform) {
    setWindowTitle(i18n::trs("设置", "Settings"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(760, 560);

    // 左侧窄导航 + 右侧内容堆叠（ChatGPT/Cursor 式骨架，分区扩展不改骨架）
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    nav_ = new QListWidget(this);
    nav_->setFixedWidth(150);
    nav_->setFocusPolicy(Qt::NoFocus);  // 去除虚线焦点框
    // 与主导航同一套矢量图标（原先这里是 emoji，和已经换成线性图标的主侧栏不一致）
    const std::vector<std::tuple<const char*, const char*, const char*>> navItems{
        {"palette", "外观", "Appearance"},
        {"database", "数据与备份", "Data & Backup"},
        {"bell", "通知偏好", "Notifications"},
        {"overview", "Agent 管理", "Agents"},
        {"plug", "Agent API", "Agent API"},
        {"refresh", "更新", "Update"},
        {"info", "关于", "About"},
    };
    for (const auto& [kind, zh, en] : navItems)
        nav_->addItem(new QListWidgetItem(ui::makeIcon(kind, ui::muted(), 16, ui::selText()),
                                          "  " + i18n::trs(zh, en)));
    root->addWidget(nav_);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildAppearancePage());
    stack_->addWidget(buildBackupPage());
    stack_->addWidget(buildNotifyPage());
    stack_->addWidget(buildAgentsPage());
    stack_->addWidget(buildApiPage());
    stack_->addWidget(buildUpdatePage());
    stack_->addWidget(buildAboutPage());
    root->addWidget(stack_, 1);

    connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    nav_->setCurrentRow(0);

    // 主题/字号变化：即时重涂导航与对话框内取色控件
    themeListenerId_ = ui::addThemeListener([this] { applyChrome(); });
    applyChrome();
}

SettingsDialog::~SettingsDialog() {
    ui::removeThemeListener(themeListenerId_);
}

void SettingsDialog::showEvent(QShowEvent*) {
    refreshBackupList();
    refreshAgents();
}

QLabel* SettingsDialog::thLabel(const QString& tmpl, QWidget* parent) {
    auto* l = new QLabel(parent);
    styledLabels_.push_back({l, tmpl});
    l->setStyleSheet(ui::th(tmpl));
    return l;
}

void SettingsDialog::applyChrome() {
    nav_->setStyleSheet(ui::th(
        "QListWidget { background:@deep@; border:none; outline:0; font-size:13px; }"
        "QListWidget::item { color:@muted@; padding:11px 12px;"
        " border-left:3px solid transparent; }"
        "QListWidget::item:hover { color:@text@; background:@card@; }"
        "QListWidget::item:selected { color:@seltext@; background:@selbg@;"
        " border-left:3px solid @brand@; font-weight:600; }"));
    for (std::size_t i = 0; i < swatches_.size() && i < ui::themes().size(); ++i)
        swatches_[i]->setSelected(static_cast<int>(i) == ui::themeIdx());
    for (const auto& [w, tmpl] : styledLabels_) w->setStyleSheet(ui::th(tmpl));
    // 图标按当前主题重新着色（换主题时导航图标也要跟着变）
    for (int i = 0; i < nav_->count() && i < 7; ++i)
        if (auto* it = nav_->item(i))
            it->setIcon(ui::makeIcon(kNavKinds[i], ui::muted(), 16, ui::selText()));
}

// ---- 外观：主题色卡网格 + 字号 ----
QWidget* SettingsDialog::buildAppearancePage() {
    auto* page = new QWidget(stack_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 20, 22, 20);
    lay->setSpacing(14);

    auto* themeTitle = thLabel("font-size:14px; font-weight:700; color:@text@;", page);
    themeTitle->setText(i18n::trs("页面颜色", "Theme"));
    lay->addWidget(themeTitle);

    // 色卡网格：每套主题一张迷你预览，点击即换全窗配色
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    for (int i = 0; i < static_cast<int>(ui::themes().size()); ++i) {
        const auto& t = ui::themes()[static_cast<std::size_t>(i)];
        auto* sw = new ui::ThemeSwatch(i18n::trs(t.zh, t.en), t.bg, t.deep, t.accent, t.brand,
                                       t.text, [i] { ui::setThemeIndex(i); }, page);
        swatches_.push_back(sw);
        grid->addWidget(sw, i / 3, i % 3, Qt::AlignTop | Qt::AlignLeft);
    }
    lay->addLayout(grid);

    auto* themeHint = thLabel("font-size:11px; color:@muted@;", page);
    themeHint->setText(i18n::trs("点击色卡立即切换整套配色，选择会被记住。",
                                 "Click a swatch to switch instantly; your choice is saved."));
    lay->addWidget(themeHint);

    auto* fontTitle = thLabel("font-size:14px; font-weight:700; color:@text@;", page);
    fontTitle->setText(i18n::trs("界面字号", "Font size"));
    lay->addWidget(fontTitle);
    fontBox_ = new QComboBox(page);
    const std::vector<std::pair<int, const char*>> kSizes{
        {12, "紧凑 12px"}, {13, "标准 13px"}, {14, "大号 14px"}};
    for (const auto& [px, label] : kSizes) fontBox_->addItem(QString::fromUtf8(label), px);
    fontBox_->setCurrentIndex(fontBox_->findData(ui::fontBaseRef()));
    connect(fontBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        ui::setFontBase(fontBox_->currentData().toInt());
    });
    fontBox_->setFixedWidth(180);
    lay->addWidget(fontBox_);

    auto* fontHint = thLabel("font-size:11px; color:@muted@;", page);
    fontHint->setText(i18n::trs("所有界面文字按所选基准等比缩放。",
                                "All UI text scales relative to the selected base size."));
    lay->addWidget(fontHint);
    lay->addStretch(1);
    return page;
}

// ---- 数据与备份：VACUUM INTO 快照 / 恢复 / 手动维护 ----
QWidget* SettingsDialog::buildBackupPage() {
    auto* page = new QWidget(stack_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 20, 22, 20);
    lay->setSpacing(12);

    auto* intro = thLabel("font-size:11px; color:@muted@;", page);
    intro->setText(i18n::trs(
        "备份是数据库的一致性快照（VACUUM INTO），保存在数据目录的 backup/ 下；"
        "恢复会用快照整库替换当前数据。",
        "Backups are consistent SQLite snapshots (VACUUM INTO) stored in the data "
        "directory's backup/ folder; restoring replaces the current database."));
    intro->setWordWrap(true);
    lay->addWidget(intro);

    backupTable_ = new QTableWidget(0, 2, page);
    backupTable_->setHorizontalHeaderLabels(
        {i18n::trs("快照文件", "Snapshot"), i18n::trs("大小", "Size")});
    backupTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    polishTable(backupTable_);
    backupTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(backupTable_, 1);

    auto* row = new QHBoxLayout();
    auto* backupBtn = new QPushButton(i18n::trs("立即备份", "Back up now"), page);
    backupBtn->setObjectName("primary");
    connect(backupBtn, &QPushButton::clicked, this, [this] {
        std::string path, err;
        if (platform_.backupCreate(path, err)) {
            ui::Toast::show(this, i18n::trs("已备份：", "Backed up: ") +
                                      QString::fromStdString(path));
            refreshBackupList();
        } else {
            ui::Toast::show(this, i18n::trs("备份失败：", "Backup failed: ") +
                                      QString::fromStdString(err), false);
        }
    });
    auto* restoreBtn = new QPushButton(i18n::trs("恢复所选", "Restore selected"), page);
    connect(restoreBtn, &QPushButton::clicked, this, [this] {
        int row = backupTable_->currentRow();
        if (row < 0) {
            ui::Toast::show(this, i18n::trs("请先选择一个快照", "Select a snapshot first"), false);
            return;
        }
        QString name = backupTable_->item(row, 0)->text();
        if (QMessageBox::warning(
                this, i18n::trs("恢复快照", "Restore snapshot"),
                i18n::trs("将用快照「%1」整库替换当前数据，未备份的更改会丢失。继续？",
                          "Replace the current database with snapshot \"%1\"? Unsaved "
                          "changes will be lost. Continue?")
                    .arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        std::string err;
        if (platform_.backupRestore(name.toStdString(), err)) {
            ui::Toast::show(this, i18n::trs("已恢复 ✓", "Restored ✓"));
            refreshBackupList();
        } else {
            ui::Toast::show(this, i18n::trs("恢复失败：", "Restore failed: ") +
                                      QString::fromStdString(err), false);
        }
    });
    auto* folderBtn = new QPushButton(i18n::trs("打开备份文件夹", "Open backup folder"), page);
    connect(folderBtn, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QString::fromStdString(platform_.homeDir()) + "/backup"));
    });
    row->addWidget(backupBtn);
    row->addWidget(restoreBtn);
    row->addWidget(folderBtn);
    row->addStretch(1);
    lay->addLayout(row);

    auto* maintBtn = new QPushButton(i18n::trs("手动维护（审计轮转 + 清理）",
                                               "Run maintenance (audit rotation + cleanup)"), page);
    connect(maintBtn, &QPushButton::clicked, this, [this] {
        std::string stats, err;
        if (platform_.maintenanceRun(zp::kManagerName, stats, err)) {
            ui::Toast::show(this, i18n::trs("维护完成", "Maintenance done"));
        } else {
            ui::Toast::show(this, i18n::trs("维护失败：", "Maintenance failed: ") +
                                      QString::fromStdString(err), false);
        }
    });
    lay->addWidget(maintBtn);
    return page;
}

// ---- 通知偏好：只影响工作台界面 ----
QWidget* SettingsDialog::buildNotifyPage() {
    auto* page = new QWidget(stack_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 20, 22, 20);
    lay->setSpacing(14);

    auto* form = new QFormLayout;
    form->setSpacing(12);
    refreshBox_ = new QComboBox(page);
    refreshBox_->addItem(i18n::trs("3 秒（实时）", "3s (live)"), 3000);
    refreshBox_->addItem(i18n::trs("5 秒", "5s"), 5000);
    refreshBox_->addItem(i18n::trs("10 秒（省电）", "10s (low power)"), 10000);
    {
        QSettings s;
        int ms = s.value("ui/refreshMs", 3000).toInt();
        int idx = refreshBox_->findData(ms);
        refreshBox_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    connect(refreshBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        QSettings s;
        s.setValue("ui/refreshMs", refreshBox_->currentData().toInt());
        ui::notifyThemeListeners();  // 主窗口监听同一通知重读偏好
    });
    form->addRow(i18n::trs("数据刷新频率", "Data refresh rate"), refreshBox_);

    errorToastBox_ = new QCheckBox(i18n::trs("出现新的未解决错误时弹出提醒",
                                             "Toast when a new unresolved error appears"), page);
    {
        QSettings s;
        errorToastBox_->setChecked(s.value("ui/errorToast", false).toBool());
    }
    connect(errorToastBox_, &QCheckBox::toggled, this, [](bool on) {
        QSettings s;
        s.setValue("ui/errorToast", on);
        ui::notifyThemeListeners();
    });
    form->addRow(errorToastBox_);
    lay->addLayout(form);

    auto* hint = thLabel("font-size:11px; color:@muted@;", page);
    hint->setText(i18n::trs("这些偏好只影响本工作台界面，不影响 Agent 侧行为。",
                            "These only affect this workbench UI, not agent-side behavior."));
    lay->addWidget(hint);
    lay->addStretch(1);
    return page;
}

// ---- Agent 管理：注册列表 / 状态 / 移除 ----
QWidget* SettingsDialog::buildAgentsPage() {
    auto* page = new QWidget(stack_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 20, 22, 20);
    lay->setSpacing(12);

    agentsTable_ = new QTableWidget(0, 5, page);
    agentsTable_->setHorizontalHeaderLabels(
        {i18n::trs("名称", "Name"), i18n::trs("角色", "Role"), i18n::trs("状态", "Status"),
         i18n::trs("当前任务", "Current task"), i18n::trs("最后活跃", "Last active")});
    agentsTable_->horizontalHeader()->setStretchLastSection(true);
    polishTable(agentsTable_);
    lay->addWidget(agentsTable_, 1);

    auto* row = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(i18n::trs("刷新", "Refresh"), page);
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refreshAgents(); });
    auto* removeBtn = new QPushButton(i18n::trs("移除所选", "Remove selected"), page);
    removeBtn->setObjectName("danger");
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        int row = agentsTable_->currentRow();
        if (row < 0) {
            ui::Toast::show(this, i18n::trs("请先选择一个 Agent", "Select an agent first"), false);
            return;
        }
        QString name = agentsTable_->item(row, 0)->text();
        if (name == QString::fromLatin1(zp::kManagerName)) {
            ui::Toast::show(this, i18n::trs("管理者不可移除", "The manager cannot be removed"),
                            false);
            return;
        }
        if (QMessageBox::question(
                this, i18n::trs("移除 Agent", "Remove agent"),
                i18n::trs("移除「%1」后其 API Key 立即失效，需要重新注册才能接入。继续？",
                          "Removing \"%1\" invalidates its API key immediately; it must "
                          "re-register to join again. Continue?")
                    .arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        std::string err;
        if (platform_.agentRemove(zp::kManagerName, name.toStdString(), err)) {
            ui::Toast::show(this, i18n::trs("已移除 ✓", "Removed ✓"));
            refreshAgents();
        } else {
            ui::Toast::show(this, i18n::trs("移除失败：", "Remove failed: ") +
                                      QString::fromStdString(err), false);
        }
    });
    row->addWidget(refreshBtn);
    row->addWidget(removeBtn);
    row->addStretch(1);
    lay->addLayout(row);

    auto* hint = thLabel("font-size:11px; color:@muted@;", page);
    hint->setText(i18n::trs("移除后该 Agent 的 API Key 立即失效（操作记入审计日志）。",
                            "Removing an agent invalidates its API key (audited)."));
    lay->addWidget(hint);
    return page;
}

void SettingsDialog::refreshBackupList() {
    if (!backupTable_) return;
    std::vector<std::string> snaps;
    std::string err;
    if (!platform_.backupList(snaps, err)) return;
    backupTable_->setRowCount(static_cast<int>(snaps.size()));
    for (size_t i = 0; i < snaps.size(); ++i) {
        const QString name = QString::fromStdString(snaps[i]);
        const qint64 sz = QFileInfo(QString::fromStdString(platform_.homeDir()) +
                                    "/backup/" + name)
                              .size();
        setRow(backupTable_, static_cast<int>(i), {name, formatNum(sz) + " B"});
    }
}

void SettingsDialog::refreshAgents() {
    if (!agentsTable_) return;
    std::vector<zp::AgentInfo> agents;
    std::string err;
    if (!platform_.listAgents(agents, err)) return;
    agentsTable_->setRowCount(static_cast<int>(agents.size()));
    for (size_t i = 0; i < agents.size(); ++i) {
        const auto& a = agents[i];
        setRow(agentsTable_, static_cast<int>(i),
               {QString::fromStdString(a.name), QString::fromStdString(a.role),
                QString::fromStdString(a.status), QString::fromStdString(a.current_task),
                a.last_seen_at.empty() ? QString("—")
                                       : relTime(QString::fromStdString(a.last_seen_at))});
    }
}

// ---- Agent API：端点清单 + 一键复制 ----
QWidget* SettingsDialog::buildApiPage() {
    auto* page = new QWidget();
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 20, 22, 20);
    lay->setSpacing(10);

    auto* addr = thLabel("font-size:13px; font-weight:600; color:@text@;", page);
    addr->setText(QString("HTTP  ·  http://127.0.0.1:%1").arg(platform_.httpPort()));
    lay->addWidget(addr);

    auto* auth = thLabel("font-size:11px; color:@muted@;", page);
    auth->setText(i18n::trs(
        "鉴权：业务接口需请求头 X-Agent-Name + X-Api-Key；"
        "预算设置等管理接口需 X-Master-Key。",
        "Auth: agent endpoints require X-Agent-Name + X-Api-Key headers; "
        "admin endpoints such as budget require X-Master-Key."));
    auth->setWordWrap(true);
    lay->addWidget(auth);

    // 关键端点列表：点击「复制」把完整 URL 放入剪贴板，便于接入新 Agent
    struct Ep {
        const char* method;
        const char* path;
    };
    const Ep eps[] = {
        {"GET", "/api/health"},
        {"POST", "/api/agents/register"},
        {"POST", "/api/agents/heartbeat"},
        {"POST", "/api/agents/remove"},
        {"GET/POST", "/api/memory"},
        {"POST", "/api/knowledge"},
        {"POST", "/api/knowledge/search"},
        {"POST", "/api/skills"},
        {"POST", "/api/skills/{name}/invoke"},
        {"POST", "/api/messages"},
        {"GET", "/api/messages"},
        {"POST", "/api/errors"},
        {"POST", "/api/usage/report"},
        {"GET", "/api/usage/summary"},
        {"GET", "/api/usage/daily"},
        {"GET", "/api/usage/models"},
        {"GET", "/api/audit"},
    };
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(5);
    for (size_t i = 0; i < std::size(eps); ++i) {
        const int r = static_cast<int>(i);
        auto* m = thLabel("color:@accent@; font-size:11px; font-family:@mono@,monospace;", page);
        m->setText(QString::fromLatin1(eps[i].method));
        auto* pth = thLabel("color:@text@; font-size:11px; font-family:@mono@,monospace;", page);
        pth->setText(QString::fromLatin1(eps[i].path));
        auto* cp = new QToolButton(page);
        cp->setText(i18n::trs("复制", "Copy"));
        cp->setCursor(Qt::PointingHandCursor);
        const QString full = QString("http://127.0.0.1:%1%2")
                                 .arg(platform_.httpPort())
                                 .arg(QString::fromLatin1(eps[i].path));
        connect(cp, &QToolButton::clicked, this, [full] {
            QApplication::clipboard()->setText(full);
            ui::Toast::show(qApp->activeWindow(), i18n::trs("已复制", "Copied"));
        });
        grid->addWidget(m, r, 0);
        grid->addWidget(pth, r, 1);
        grid->addWidget(cp, r, 2);
        grid->setColumnStretch(1, 1);
    }
    lay->addLayout(grid);
    lay->addStretch(1);

    auto* scroll = new QScrollArea(stack_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

// ---- 关于：品牌触点（口号定稿见 docs/brand.md）----
QWidget* SettingsDialog::buildAboutPage() {
    auto* page = new QWidget(stack_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 20, 22, 20);
    lay->addStretch(2);

    auto* logo = new QLabel(page);
    QPixmap pm(":/brand/logo.png");
    logo->setPixmap(pm.scaled(84, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);
    lay->addWidget(logo);

    auto* name = thLabel("font-size:20px; font-weight:800; color:@text@;", page);
    name->setText("AgentHive");
    name->setAlignment(Qt::AlignCenter);
    lay->addWidget(name);

    auto* slogan = thLabel("font-size:13px; font-weight:600; color:@brand@;", page);
    slogan->setText(i18n::trs("单体成长，蜂巢共享", "Grow alone, thrive together."));
    slogan->setAlignment(Qt::AlignCenter);
    lay->addWidget(slogan);

    auto* ver = thLabel("font-size:11px; color:@muted@;", page);
    ver->setText(QString("v%1  ·  %2")
                     .arg(zp::kPlatformVersion)
                     .arg(i18n::trs("本地优先 · 数据不出本机", "local-first · data stays local")));
    ver->setAlignment(Qt::AlignCenter);
    lay->addWidget(ver);
    lay->addSpacing(10);

    // 快捷键速查：随主题重涂的富文本标签（键帽用内联样式，QSS 管不到富文本内部）
    auto* keys = new QLabel(page);
    keys->setTextFormat(Qt::RichText);
    keys->setAlignment(Qt::AlignCenter);
    keys->setStyleSheet(ui::th("font-size:11px; color:@muted@;"));
    const auto kbd = [](const QString& k) {
        return ui::th(QString("<span style='background:@field@; color:@text@;"
                              " border:1px solid @line@; border-radius:4px;"
                              " padding:0 4px; font-family:@mono@,monospace;'>%1</span>")
                          .arg(k));
    };
    keys->setText(QString(
        "%1 %2　%3 %4　%5 %6　%7 %8")
        .arg(kbd("Ctrl 1-7"), i18n::trs("切换面板", "switch panel"))
        .arg(kbd("F5"), i18n::trs("刷新", "refresh"))
        .arg(kbd("Ctrl+F"), i18n::trs("聚焦过滤框", "focus filter"))
        .arg(kbd("Ctrl+,"), i18n::trs("打开设置", "open settings")));
    lay->addWidget(keys);
    lay->addSpacing(2);

    auto* row = new QHBoxLayout();
    row->addStretch(1);
    auto mk = [this, page, row](const QString& text, const char* url) {
        auto* b = new QPushButton(text, page);
        connect(b, &QPushButton::clicked, this,
                [url] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(url))); });
        row->addWidget(b);
        return b;
    };
    mk(i18n::trs("GitHub 仓库", "GitHub"), "https://github.com/SiliconCoderJames/AgentHive");
    mk(i18n::trs("问题反馈", "Issues"), "https://github.com/SiliconCoderJames/AgentHive/issues");
    mk(i18n::trs("☕ 赞助", "Sponsor"), "https://www.buymeacoffee.com/zwj8jc5rrgp");
    row->addStretch(1);
    lay->addLayout(row);

    auto* lic = thLabel("font-size:10px; color:@muted@;", page);
    lic->setText("© 2026 SiliconCoderJames · MIT License");
    lic->setAlignment(Qt::AlignCenter);
    lay->addWidget(lic);
    lay->addStretch(3);
    return page;
}

// ---- 更新：当前版本 + GitHub Releases 检查 ----
QWidget* SettingsDialog::buildUpdatePage() {
    auto* page = new QWidget(stack_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 20, 22, 20);
    lay->setSpacing(10);

    auto* cur = thLabel("font-size:13px; font-weight:600; color:@text@;", page);
    cur->setText(QString(i18n::trs("当前版本", "Current version")) +
                 QString("  v%1").arg(zp::kPlatformVersion));
    lay->addWidget(cur);

    auto* row = new QHBoxLayout();
    auto* checkBtn = new QPushButton(i18n::trs("检查更新", "Check for updates"), page);
    connect(checkBtn, &QPushButton::clicked, this, [this] {
        if (!net_) net_ = new QNetworkAccessManager(this);
        latest_->setText(i18n::trs("正在检查…", "Checking…"));
        QNetworkRequest req(QUrl(QString::fromLatin1(kRepoApi)));
        req.setHeader(QNetworkRequest::UserAgentHeader, "AgentHive");
        req.setTransferTimeout(8000);
        QNetworkReply* reply = net_->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                latest_->setText(i18n::trs("检查失败：", "Check failed: ") + reply->errorString());
                return;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QString tag = doc.object().value("tag_name").toString();
            if (tag.startsWith('v')) tag = tag.mid(1);
            const QString curV = QString::fromLatin1(zp::kPlatformVersion);
            if (tag.isEmpty()) {
                latest_->setText(i18n::trs("未解析到最新版本信息。",
                                           "Could not parse release info."));
            } else if (cmpVersion(tag, curV) > 0) {
                latest_->setText(i18n::trs("发现新版本", "New version available") +
                                 QString("  v%1").arg(tag));
            } else {
                latest_->setText(i18n::trs("已是最新版本。", "You're up to date.") +
                                 QString("  v%1").arg(curV));
            }
        });
    });
    auto* pageBtn = new QPushButton(i18n::trs("Releases 页面", "Releases page"), page);
    connect(pageBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(kRepoPage)));
    });
    row->addWidget(checkBtn);
    row->addWidget(pageBtn);
    row->addStretch(1);
    lay->addLayout(row);

    latest_ = thLabel("font-size:12px; color:@muted@;", page);
    latest_->setWordWrap(true);
    lay->addWidget(latest_);

    auto* note = thLabel("font-size:11px; color:@muted@;", page);
    note->setText(i18n::trs("AgentHive 纯本地运行、无遥测；不自动下载更新。",
                            "AgentHive runs locally with no telemetry; it never auto-updates."));
    note->setWordWrap(true);
    lay->addWidget(note);
    lay->addStretch(1);
    return page;
}
