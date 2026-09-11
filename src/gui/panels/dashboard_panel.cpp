#include "dashboard_panel.h"

#include <algorithm>

#include <QGroupBox>

#include "../gui_util.h"
#include "../i18n.h"

DashboardPanel::DashboardPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    buildHeader(root, "总览", "Overview", "预算消耗 · Agent 状态 · 事件流与告警，一屏掌握蜂巢动态",
                "Budget, agent status, event stream and alerts at a glance");

    // ---- 第一行：Token 环形图 + 各 Agent 用量柱状图 ----
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(14);

    budgetCard_ = new QGroupBox(i18n::trs("本周 Token 预算", "Weekly Token Budget"), this);
    budgetCard_->setObjectName("card");
    auto* bl = new QVBoxLayout(budgetCard_);
    bl->setContentsMargins(10, 18, 10, 8);
    ring_ = new ui::RingProgress(budgetCard_);
    bl->addWidget(ring_, 1);
    topRow->addWidget(budgetCard_, 2);

    usageCard_ = new QGroupBox(i18n::trs("各 Agent 本周用量", "Per-Agent Usage This Week"), this);
    usageCard_->setObjectName("card");
    auto* ul = new QVBoxLayout(usageCard_);
    ul->setContentsMargins(10, 18, 10, 8);
    usageChart_ = new ui::HBarChart(usageCard_);
    ul->addWidget(usageChart_, 1);
    topRow->addWidget(usageCard_, 3);
    root->addLayout(topRow, 3);

    // ---- 第二行：Agent 状态卡片网格 ----
    agentsCard_ = new QGroupBox(i18n::trs("Agent 状态", "Agent Status"), this);
    agentsCard_->setObjectName("card");
    auto* al = new QVBoxLayout(agentsCard_);
    al->setContentsMargins(10, 18, 10, 8);
    auto* gridHolder = new QWidget(agentsCard_);
    // 透明底色：否则全局 QSS 的 QWidget 背景会在卡片内再涂一层页面底色，形成"卡中卡"暗框
    gridHolder->setStyleSheet("background:transparent;");
    agentGrid_ = new QGridLayout(gridHolder);
    agentGrid_->setContentsMargins(0, 0, 0, 0);
    agentGrid_->setSpacing(10);
    al->addWidget(gridHolder);
    al->addStretch(1);
    // Agent 状态按内容取高（不吸收多余空间）：Agent 少时不再撑出一大片空白
    root->addWidget(agentsCard_, 0);

    // ---- 第三行：逐日 Token 趋势 + 模型用量（自设置迁移至总览，主窗口直接可读）----
    auto* trendRow = new QHBoxLayout();
    trendRow->setSpacing(14);
    trendCard_ = new QGroupBox(i18n::trs("最近 14 天逐日消耗", "Daily Tokens (14 days)"), this);
    trendCard_->setObjectName("card");
    auto* trl = new QVBoxLayout(trendCard_);
    trl->setContentsMargins(10, 18, 10, 8);
    trendChart_ = new ui::VBarChart(trendCard_);
    trl->addWidget(trendChart_, 1);
    trendRow->addWidget(trendCard_, 3);
    modelCard_ = new QGroupBox(i18n::trs("模型用量累计", "Tokens by Model"), this);
    modelCard_->setObjectName("card");
    auto* ml = new QVBoxLayout(modelCard_);
    ml->setContentsMargins(10, 18, 10, 8);
    modelChart_ = new ui::HBarChart(modelCard_);
    ml->addWidget(modelChart_, 1);
    trendRow->addWidget(modelCard_, 2);
    root->addLayout(trendRow, 3);

    // ---- 第四行：事件流时间线 + 告警卡片 ----
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(14);

    tlCard_ = new QGroupBox(i18n::trs("事件流", "Event Stream"), this);
    tlCard_->setObjectName("card");
    auto* tl = new QVBoxLayout(tlCard_);
    tl->setContentsMargins(10, 18, 10, 8);
    timeline_ = new QListWidget(tlCard_);
    timeline_->setAlternatingRowColors(true);
    // 经 ui::th() 生成：等宽字体族与字号随主题/字号档位缩放（原先硬编码 11px 不随动）
    timeline_->setStyleSheet(ui::th(
        "QListWidget { font-family:@mono@,monospace; font-size:11px; }"
        "QListWidget::item { padding:3px 4px; }"));
    tl->addWidget(timeline_);
    bottomRow->addWidget(tlCard_, 3);

    alertCard_ = new QGroupBox(i18n::trs("告警", "Alerts"), this);
    alertCard_->setObjectName("card");
    auto* wl = new QVBoxLayout(alertCard_);
    wl->setContentsMargins(10, 18, 10, 8);
    wl->setSpacing(8);
    emptyAlerts_ = new QLabel(i18n::trs("暂无错误，一切正常 ✓", "No errors — all clear ✓"), alertCard_);
    emptyAlerts_->setStyleSheet(ui::th("color:@ok@; font-size:13px;"));
    emptyAlerts_->setAlignment(Qt::AlignCenter);
    wl->addWidget(emptyAlerts_);
    alertsLay_ = wl;
    bottomRow->addWidget(alertCard_, 2);
    root->addLayout(bottomRow, 3);
}

void DashboardPanel::refresh() {
    std::string err;

    // 预算环形图 + 用量柱状图
    zp::UsageSummary sum;
    if (platform_.usageSummary(sum, err)) {
        ring_->setValues(sum.total_tokens, sum.budget, i18n::trs("剩余 %1", "left %1")
                                                          .arg(formatNum(sum.budget - sum.total_tokens)));
        QVector<QPair<QString, qint64>> bars;
        for (const auto& [name, tokens] : sum.per_agent)
            bars.append({QString::fromStdString(name), tokens});
        usageChart_->setEntries(bars);

        budgetAlertLevel_ = QString::fromStdString(sum.alert_level);
    }

    // Agent 状态卡片网格（列数随可用宽度自适应；卡片池复用避免闪烁）
    std::vector<zp::AgentInfo> agents;
    if (platform_.listAgents(agents, err)) {
        size_t need = agents.size();
        while (agentCards_.size() < need)
            agentCards_.push_back(new ui::AgentCard(this));
        // 窄窗口降到 1~2 列，避免卡片被压到读不出内容
        const int avail = agentsCard_->width() > 0 ? agentsCard_->width() : width();
        const int cols = qBound(1, avail / 300, 3);
        for (size_t i = 0; i < agentCards_.size(); ++i) {
            auto* card = agentCards_[i];
            agentGrid_->removeWidget(card);  // 列数变化时重新落位
            if (i < need) {
                agentGrid_->addWidget(card, static_cast<int>(i) / cols, static_cast<int>(i) % cols);
                card->setVisible(true);
                const auto& a = agents[i];
                const QString rawSeen = QString::fromStdString(a.last_seen_at);
                card->setAgent(QString::fromStdString(a.name), QString::fromStdString(a.status),
                               QString::fromStdString(a.role), QString::fromStdString(a.current_task),
                               rawSeen.isEmpty() ? QString() : relTime(rawSeen), localStamp(rawSeen));
            } else {
                card->hide();
            }
        }
    }

    // 逐日趋势 + 模型用量（数据变化才重绘，避免 3s 刷新反复扫掠动画）
    std::vector<zp::UsageDailyPoint> dailyPts;
    if (platform_.usageDaily(14, dailyPts, err)) {
        QVector<QPair<QString, qint64>> es;
        for (const auto& d : dailyPts)
            es.push_back({QString::fromStdString(d.day).mid(5), d.tokens});  // MM-DD
        if (es != lastDaily_) {
            lastDaily_ = es;
            trendChart_->setEntries(es);
        }
    }
    std::vector<zp::UsageModelRow> modelRows;
    if (platform_.usageByModel(modelRows, err)) {
        QVector<QPair<QString, qint64>> es;
        for (size_t i = 0; i < modelRows.size() && i < 8; ++i)
            es.push_back({QString::fromStdString(modelRows[i].model), modelRows[i].tokens});
        if (es != lastModel_) {
            lastModel_ = es;
            modelChart_->setEntries(es);
        }
    }

    // 告警统一重建：预算卡 + 未解决错误分级卡片；无告警时显示空状态
    for (auto* w : alertCards_) {
        alertsLay_->removeWidget(w);
        w->deleteLater();
    }
    alertCards_.clear();
    if (budgetAlertLevel_ == "warn")
        alertCards_.push_back(new ui::AlertCard(
            "warn", i18n::trs("Token 用量已达预算 80%，请留意消耗",
                              "Token usage reached 80% of budget"),
            this));
    else if (budgetAlertLevel_ == "critical")
        alertCards_.push_back(new ui::AlertCard(
            "critical", i18n::trs("Token 用量已达预算 95%！", "Token usage reached 95% of budget!"),
            this));
    else if (budgetAlertLevel_ == "over")
        alertCards_.push_back(new ui::AlertCard(
            "critical", i18n::trs("Token 用量已超出本周预算！",
                                  "Token usage exceeded this week's budget!"),
            this));
    std::vector<zp::ErrorReport> openErrors;
    platform_.errorList("open", "", 10, openErrors, err);
    for (const auto& e : openErrors) {
        const QString when = relTime(QString::fromStdString(e.created_at));
        alertCards_.push_back(new ui::AlertCard(
            e.severity == "critical" ? "critical" : (e.severity == "warning" ? "warn" : "note"),
            QString("%1 — %2 · %3")
                .arg(QString::fromStdString(e.title))
                .arg(QString::fromStdString(e.reporter))
                .arg(when),
            this));
    }
    int at = alertsLay_->indexOf(emptyAlerts_);
    for (auto* card : alertCards_) {
        int insertAt = at < 0 ? alertsLay_->count() : at;
        alertsLay_->insertWidget(insertAt, card);
        ++at;
    }
    emptyAlerts_->setVisible(alertCards_.empty());

    // 事件流时间线（最近 15 条审计）
    std::vector<zp::AuditRecord> records;
    if (platform_.auditList("", "", "", 15, records, err)) {
        // 动作 → 与侧栏同源的矢量图标种类（替代 emoji：跨平台形状一致、随主题着色）
        auto iconKind = [](const std::string& a, const std::string& t) -> const char* {
            const std::string s = a + " " + t;
            if (s.find("error") != std::string::npos) return "errors";
            if (s.find("skill") != std::string::npos) return "skills";
            if (s.find("memory") != std::string::npos) return "memory";
            if (s.find("knowledge") != std::string::npos) return "knowledge";
            if (s.find("note") != std::string::npos || s.find("question") != std::string::npos ||
                s.find("task") != std::string::npos || s.find("message") != std::string::npos)
                return "messages";
            if (s.find("register") != std::string::npos || s.find("agent") != std::string::npos)
                return "overview";
            return "audit";
        };
        auto iconColor = [](const std::string& a) {
            if (a.find("error") != std::string::npos) return ui::danger();
            if (a.find("skill") != std::string::npos) return ui::warn();
            if (a.find("memory") != std::string::npos) return ui::selText();
            if (a.find("knowledge") != std::string::npos) return ui::accent();
            if (a.find("agent") != std::string::npos) return ui::brand();
            return ui::muted();
        };
        QString joined;
        for (const auto& r : records)
            joined += QString("%1|%2|%3|%4\n")
                          .arg(QString::fromStdString(r.created_at))
                          .arg(QString::fromStdString(r.actor))
                          .arg(QString::fromStdString(r.action))
                          .arg(QString::fromStdString(r.target));
        if (joined != lastTimeline_) {  // 内容未变时不重建，避免 3s 刷新闪烁
            lastTimeline_ = joined;
            timeline_->clear();
            for (const auto& r : records) {
                const QString raw = QString::fromStdString(r.created_at);
                auto* item = new QListWidgetItem(
                    ui::makeIcon(iconKind(r.action, r.target), iconColor(r.action), 14),
                    QString("%1  %2  %3  %4")
                        .arg(relTime(raw), -10)  // 相对时间等宽对齐，扫读更快
                        .arg(QString::fromStdString(r.actor))
                        .arg(QString::fromStdString(r.action))
                        .arg(QString::fromStdString(r.target)));
                item->setToolTip(QString("%1\n%2 · %3 · %4")
                                     .arg(localStamp(raw))
                                     .arg(QString::fromStdString(r.actor))
                                     .arg(QString::fromStdString(r.action))
                                     .arg(QString::fromStdString(r.target)));
                timeline_->addItem(item);
            }
        }
    }
}

void DashboardPanel::retranslate() {
    PanelBase::retranslate();
    budgetCard_->setTitle(i18n::trs("本周 Token 预算", "Weekly Token Budget"));
    usageCard_->setTitle(i18n::trs("各 Agent 本周用量", "Per-Agent Usage This Week"));
    agentsCard_->setTitle(i18n::trs("Agent 状态", "Agent Status"));
    trendCard_->setTitle(i18n::trs("最近 14 天逐日消耗", "Daily Tokens (14 days)"));
    modelCard_->setTitle(i18n::trs("模型用量累计", "Tokens by Model"));
    tlCard_->setTitle(i18n::trs("事件流", "Event Stream"));
    alertCard_->setTitle(i18n::trs("告警", "Alerts"));
    emptyAlerts_->setText(i18n::trs("暂无错误，一切正常 ✓", "No errors — all clear ✓"));
    lastTimeline_.clear();  // 强制事件流下次刷新重建（文案随语言变化）
}
