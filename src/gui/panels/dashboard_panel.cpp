#include "dashboard_panel.h"

#include <algorithm>

#include <QGroupBox>

#include "../gui_util.h"
#include "../i18n.h"

DashboardPanel::DashboardPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(14);
    buildHeader(root, "总览", "Overview", "预算消耗 · Agent 状态 · 事件流与告警，一屏掌握蜂巢动态",
                "Budget, agent status, event stream and alerts at a glance");

    // ---- 第一行：Token 环形图 + 各 Agent 用量柱状图 ----
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(14);

    budgetCard_ = new QGroupBox(i18n::trs("本周 Token 预算", "Weekly Token Budget"), this);
    budgetCard_->setObjectName("card");
    auto* bl = new QVBoxLayout(budgetCard_);
    bl->setContentsMargins(12, 20, 12, 12);
    ring_ = new ui::RingProgress(budgetCard_);
    bl->addWidget(ring_, 1);
    topRow->addWidget(budgetCard_, 2);

    usageCard_ = new QGroupBox(i18n::trs("各 Agent 本周用量", "Per-Agent Usage This Week"), this);
    usageCard_->setObjectName("card");
    auto* ul = new QVBoxLayout(usageCard_);
    ul->setContentsMargins(12, 20, 12, 12);
    usageChart_ = new ui::HBarChart(usageCard_);
    ul->addWidget(usageChart_, 1);
    topRow->addWidget(usageCard_, 3);
    root->addLayout(topRow, 2);

    // ---- 第二行：Agent 状态卡片网格 ----
    agentsCard_ = new QGroupBox(i18n::trs("Agent 状态", "Agent Status"), this);
    agentsCard_->setObjectName("card");
    auto* al = new QVBoxLayout(agentsCard_);
    al->setContentsMargins(12, 20, 12, 12);
    auto* gridHolder = new QWidget(agentsCard_);
    agentGrid_ = new QGridLayout(gridHolder);
    agentGrid_->setContentsMargins(0, 0, 0, 0);
    agentGrid_->setSpacing(10);
    al->addWidget(gridHolder);
    al->addStretch(1);
    root->addWidget(agentsCard_, 3);

    // ---- 第三行：事件流时间线 + 告警卡片 ----
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(14);

    tlCard_ = new QGroupBox(i18n::trs("事件流", "Event Stream"), this);
    tlCard_->setObjectName("card");
    auto* tl = new QVBoxLayout(tlCard_);
    tl->setContentsMargins(12, 20, 12, 12);
    timeline_ = new QListWidget(tlCard_);
    timeline_->setAlternatingRowColors(true);
    timeline_->setStyleSheet("QListWidget { font-family: Consolas,monospace; font-size: 11px; }"
                             "QListWidget::item { padding: 3px 2px; }");
    tl->addWidget(timeline_);
    bottomRow->addWidget(tlCard_, 3);

    alertCard_ = new QGroupBox(i18n::trs("告警", "Alerts"), this);
    alertCard_->setObjectName("card");
    auto* wl = new QVBoxLayout(alertCard_);
    wl->setContentsMargins(12, 20, 12, 12);
    wl->setSpacing(8);
    emptyAlerts_ = new QLabel(i18n::trs("暂无错误，一切正常 ✓", "No errors — all clear ✓"), alertCard_);
    emptyAlerts_->setStyleSheet(ui::th("color:@ok@; font-size:13px;"));
    emptyAlerts_->setAlignment(Qt::AlignCenter);
    wl->addWidget(emptyAlerts_);
    alertsLay_ = wl;
    bottomRow->addWidget(alertCard_, 2);
    root->addLayout(bottomRow, 2);
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

    // Agent 状态卡片网格（3 列；卡片池复用避免闪烁）
    std::vector<zp::AgentInfo> agents;
    if (platform_.listAgents(agents, err)) {
        size_t need = agents.size();
        while (agentCards_.size() < need)
            agentCards_.push_back(new ui::AgentCard(this));
        for (size_t i = 0; i < agentCards_.size(); ++i) {
            auto* card = agentCards_[i];
            if (i < need) {
                agentGrid_->addWidget(card, static_cast<int>(i / 3), static_cast<int>(i % 3));
                card->setVisible(true);
                const auto& a = agents[i];
                card->setAgent(QString::fromStdString(a.name), QString::fromStdString(a.status),
                               QString::fromStdString(a.role), QString::fromStdString(a.current_task),
                               QString::fromStdString(a.last_seen_at));
            } else {
                agentGrid_->removeWidget(card);
                card->hide();
            }
        }
    }

    // 告警统一重建：预算卡 + 未解决错误分级卡片；无告警时显示空状态
    for (auto* w : alertCards_) {
        alertsLay_->removeWidget(w);
        w->deleteLater();
    }
    alertCards_.clear();
    if (budgetAlertLevel_ == "warn")
        alertCards_.push_back(new ui::AlertCard("warn", "⚠ Token 用量已达预算 80%，请控制消耗", this));
    else if (budgetAlertLevel_ == "critical")
        alertCards_.push_back(new ui::AlertCard("critical", "⛔ Token 用量已达预算 95%！", this));
    else if (budgetAlertLevel_ == "over")
        alertCards_.push_back(new ui::AlertCard("critical", "🚫 Token 用量已超出本周预算！", this));
    std::vector<zp::ErrorReport> openErrors;
    platform_.errorList("open", "", 10, openErrors, err);
    for (const auto& e : openErrors) {
        alertCards_.push_back(new ui::AlertCard(
            e.severity == "critical" ? "critical" : (e.severity == "warning" ? "warn" : "note"),
            QString("[%1] %2 — %3")
                .arg(QString::fromStdString(e.severity))
                .arg(QString::fromStdString(e.title))
                .arg(QString::fromStdString(e.reporter)),
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
        // 按动作类型着色：知识库蓝 / 技能橙 / 记忆紫 / 消息绿 / 错误红 / 注册金
        auto icon = [](const std::string& a, const std::string& t) {
            std::string s = a + " " + t;
            if (s.find("error") != std::string::npos) return "🚨";
            if (s.find("skill") != std::string::npos) return "🧩";
            if (s.find("memory") != std::string::npos) return "🧠";
            if (s.find("knowledge") != std::string::npos) return "📚";
            if (s.find("note") != std::string::npos || s.find("question") != std::string::npos ||
                s.find("task") != std::string::npos)
                return "💬";
            if (s.find("register") != std::string::npos || s.find("agent") != std::string::npos)
                return "🐝";
            return "▸";
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
                auto* item = new QListWidgetItem(QString("%1  %2 %3  %4 %5")
                                                     .arg(QString::fromStdString(r.created_at))
                                                     .arg(icon(r.action, r.target))
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
    tlCard_->setTitle(i18n::trs("事件流", "Event Stream"));
    alertCard_->setTitle(i18n::trs("告警", "Alerts"));
    emptyAlerts_->setText(i18n::trs("暂无错误，一切正常 ✓", "No errors — all clear ✓"));
    lastTimeline_.clear();  // 强制事件流下次刷新重建（文案随语言变化）
}
