#include "settings_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <iterator>

#include "gui_util.h"
#include "i18n.h"
#include "theme.h"

namespace {
constexpr const char* kRepoApi =
    "https://api.github.com/repos/SiliconCoderJames/AgentHive/releases/latest";
constexpr const char* kRepoPage = "https://github.com/SiliconCoderJames/AgentHive/releases";

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
    resize(640, 600);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 14, 14, 14);
    lay->setSpacing(10);

    auto* tabs = new QTabWidget(this);
    buildAppearanceTab(tabs);
    buildUpdateTab(tabs);
    buildApiTab(tabs);
    buildUsageTab(tabs);
    lay->addWidget(tabs);

    auto* foot = new QHBoxLayout();
    foot->addStretch(1);
    auto* closeBtn = new QPushButton(i18n::trs("关闭", "Close"), this);
    closeBtn->setFixedWidth(88);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    foot->addWidget(closeBtn);
    lay->addLayout(foot);

    // 主题/字号变化：即时重涂对话框内取色控件与图表
    themeListenerId_ = ui::addThemeListener([this] { applyChrome(); });
    applyChrome();

    // 用量页每 5s 静默刷新（数据变化才重绘，避免动画反复扫掠）
    usageTimer_ = new QTimer(this);
    usageTimer_->setInterval(5000);
    connect(usageTimer_, &QTimer::timeout, this, &SettingsDialog::refreshUsage);
    usageTimer_->start();
    refreshUsage();
}

SettingsDialog::~SettingsDialog() {
    ui::removeThemeListener(themeListenerId_);
}

QLabel* SettingsDialog::thLabel(const QString& tmpl, QWidget* parent) {
    auto* l = new QLabel(parent);
    styledLabels_.push_back({l, tmpl});
    l->setStyleSheet(ui::th(tmpl));
    return l;
}

void SettingsDialog::applyChrome() {
    for (const auto& [w, tmpl] : styledLabels_) w->setStyleSheet(ui::th(tmpl));
    dailyChart_->update();
    modelChart_->update();
}

void SettingsDialog::buildAppearanceTab(QTabWidget* tabs) {
    auto* page = new QWidget(tabs);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(18, 18, 18, 18);
    form->setSpacing(14);

    themeBox_ = new QComboBox(page);
    for (const auto& t : ui::themes()) themeBox_->addItem(i18n::trs(t.zh, t.en));
    themeBox_->setCurrentIndex(ui::themeIdx());
    connect(themeBox_, &QComboBox::currentIndexChanged, this,
            [](int i) { ui::setThemeIndex(i); });
    form->addRow(i18n::trs("配色主题", "Theme"), themeBox_);

    fontBox_ = new QComboBox(page);
    const std::vector<std::pair<int, const char*>> kSizes{
        {12, "紧凑 12px"}, {13, "标准 13px"}, {14, "大号 14px"}};
    for (const auto& [px, label] : kSizes)
        fontBox_->addItem(QString::fromUtf8(label), px);
    fontBox_->setCurrentIndex(fontBox_->findData(ui::fontBaseRef()));
    connect(fontBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        ui::setFontBase(fontBox_->currentData().toInt());
    });
    form->addRow(i18n::trs("界面字号", "Font size"), fontBox_);

    auto* hint = thLabel("font-size:11px; color:@muted@;", page);
    hint->setText(i18n::trs("切换后立即生效并保存，重启后仍保持。",
                            "Applied instantly and saved across restarts."));
    hint->setWordWrap(true);
    form->addRow(hint);
    tabs->addTab(page, i18n::trs("外观", "Appearance"));
}

void SettingsDialog::buildUpdateTab(QTabWidget* tabs) {
    auto* page = new QWidget(tabs);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(18, 18, 18, 18);
    lay->setSpacing(10);

    auto* cur = thLabel("font-size:13px; font-weight:600; color:@text@;", page);
    cur->setText(QString(i18n::trs("当前版本", "Current version")) +
                 QString("  v%1").arg(zp::kPlatformVersion));
    lay->addWidget(cur);

    auto* row = new QHBoxLayout();
    auto* checkBtn = new QPushButton(i18n::trs("检查更新", "Check for updates"), page);
    connect(checkBtn, &QPushButton::clicked, this, &SettingsDialog::checkUpdate);
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
    lay->addStretch(1);
    tabs->addTab(page, i18n::trs("更新", "Update"));
}

void SettingsDialog::checkUpdate() {
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
        const QString cur = QString::fromLatin1(zp::kPlatformVersion);
        if (tag.isEmpty()) {
            latest_->setText(i18n::trs("未解析到最新版本信息。",
                                       "Could not parse release info."));
        } else if (cmpVersion(tag, cur) > 0) {
            latest_->setText(i18n::trs("发现新版本", "New version available") +
                             QString("  v%1").arg(tag));
        } else {
            latest_->setText(i18n::trs("已是最新版本。", "You're up to date.") +
                             QString("  v%1").arg(cur));
        }
    });
}

void SettingsDialog::buildApiTab(QTabWidget* tabs) {
    auto* page = new QWidget();
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(18, 18, 18, 18);
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

    auto* scroll = new QScrollArea(tabs);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    tabs->addTab(scroll, i18n::trs("Agent API", "Agent API"));
}

void SettingsDialog::buildUsageTab(QTabWidget* tabs) {
    auto* page = new QWidget(tabs);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(18, 18, 18, 18);
    lay->setSpacing(12);

    usageHead_ = thLabel("font-size:12px; color:@muted@;", page);
    lay->addWidget(usageHead_);

    auto* dayTitle = thLabel("font-size:12px; font-weight:600; color:@text@;", page);
    dayTitle->setText(i18n::trs("最近 14 天逐日消耗（Token）", "Last 14 days (tokens)"));
    lay->addWidget(dayTitle);
    dailyChart_ = new ui::VBarChart(page);
    lay->addWidget(dailyChart_);

    auto* modelTitle = thLabel("font-size:12px; font-weight:600; color:@text@;", page);
    modelTitle->setText(i18n::trs("按模型累计（Top 8）", "By model (top 8)"));
    lay->addWidget(modelTitle);
    modelChart_ = new ui::HBarChart(page);
    modelChart_->setMinimumHeight(140);
    lay->addWidget(modelChart_);

    tabs->addTab(page, i18n::trs("用量统计", "Usage"));
}

void SettingsDialog::refreshUsage() {
    if (!isVisible()) return;  // 隐藏时不空转
    std::string err;
    zp::UsageSummary sum;
    if (platform_.usageSummary(sum, err)) {
        double pct = sum.budget > 0 ? 100.0 * sum.total_tokens / sum.budget : 0.0;
        const QString head =
            QString(i18n::trs("本周", "This week")) +
            QString("  %1 / %2  (%3%)")
                .arg(formatNum(sum.total_tokens))
                .arg(formatNum(sum.budget))
                .arg(pct, 0, 'f', 1);
        if (head != lastHead_) {
            lastHead_ = head;
            usageHead_->setText(head);
        }
    }
    std::vector<zp::UsageDailyPoint> pts;
    if (platform_.usageDaily(14, pts, err)) {
        QVector<QPair<QString, qint64>> es;
        es.reserve(static_cast<qsizetype>(pts.size()));
        for (const auto& pt : pts) es.push_back({QString::fromStdString(pt.day), pt.tokens});
        if (es != lastDaily_) {
            lastDaily_ = es;
            dailyChart_->setEntries(es);
        }
    }
    std::vector<zp::UsageModelRow> rows;
    if (platform_.usageByModel(rows, err)) {
        QVector<QPair<QString, qint64>> es;
        for (size_t i = 0; i < rows.size() && i < 8; ++i)
            es.push_back({QString::fromStdString(rows[i].model), rows[i].tokens});
        if (es != lastModel_) {
            lastModel_ = es;
            modelChart_->setEntries(es);
        }
    }
}
