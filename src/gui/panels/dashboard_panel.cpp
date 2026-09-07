#include "dashboard_panel.h"

#include <algorithm>

#include <QGroupBox>

DashboardPanel::DashboardPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(14);

    // ---- 第一行：Token 环形图 + 各 Agent 用量柱状图 ----
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(14);

    auto* budgetCard = new QGroupBox("本周 Token 预算", this);
    budgetCard->setObjectName("card");
    auto* bl = new QVBoxLayout(budgetCard);
    bl->setContentsMargins(12, 20, 12, 12);
    ring_ = new ui::RingProgress(budgetCard);
    bl->addWidget(ring_, 1);
    topRow->addWidget(budgetCard, 2);

    auto* usageCard = new QGroupBox("各 Agent 本周用量", this);
    usageCard->setObjectName("card");
    auto* ul = new QVBoxLayout(usageCard);
    ul->setContentsMargins(12, 20, 12, 12);
    usageChart_ = new ui::HBarChart(usageCard);
    ul->addWidget(usageChart_, 1);
    topRow->addWidget(usageCard, 3);
    root->addLayout(topRow, 2);

    // ---- 第二行：Agent 状态卡片网格 ----
    auto* agentsCard = new QGroupBox("Agent 状态", this);
    agentsCard->setObjectName("card");
    auto* al = new QVBoxLayout(agentsCard);
    al->setContentsMargins(12, 20, 12, 12);
    auto* gridHolder = new QWidget(agentsCard);
    agentGrid_ = new QGridLayout(gridHolder);
    agentGrid_->setContentsMargins(0, 0, 0, 0);
    agentGrid_->setSpacing(10);
    al->addWidget(gridHolder);
    al->addStretch(1);
    root->addWidget(agentsCard, 3);

    // ---- 第三行：事件流时间线 + 告警卡片 ----
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(14);

    auto* tlCard = new QGroupBox("事件流", this);
    tlCard->setObjectName("card");
    auto* tl = new QVBoxLayout(tlCard);
    tl->setContentsMargins(12, 20, 12, 12);
    timeline_ = new QListWidget(tlCard);
    timeline_->setAlternatingRowColors(true);
    timeline_->setStyleSheet("QListWidget { font-family: Consolas,monospace; font-size: 11px; }"
                             "QListWidget::item { padding: 3px 2px; }");
    tl->addWidget(timeline_);
    bottomRow->addWidget(tlCard, 3);

    auto* alertCard = new QGroupBox("告警", this);
    alertCard->setObjectName("card");
    auto* wl = new QVBoxLayout(alertCard);
    wl->setContentsMargins(12, 20, 12, 12);
    wl->setSpacing(8);
    emptyAlerts_ = new QLabel("暂无错误，一切正常 ✓", alertCard);
    emptyAlerts_->setStyleSheet("color:#22c55e; font-size:13px;");
    emptyAlerts_->setAlignment(Qt::AlignCenter);
    wl->addWidget(emptyAlerts_);
    alertsLay_ = wl;
    bottomRow->addWidget(alertCard, 2);
    root->addLayout(bottomRow, 2);
}

void DashboardPanel::refresh() {
    std::string err;

    // 预算环形图 + 用量柱状图
    zp::UsageSummary sum;
    if (platform_.usageSummary(sum, err)) {
        ring_->setValues(sum.total_tokens, sum.budget, QString("剩余 %1")
                                                          .arg(QString::number(sum.budget - sum.total_tokens)));
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
        timeline_->clear();
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
