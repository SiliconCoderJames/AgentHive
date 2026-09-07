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
    timeline_->setStyleSheet("QListWidget { font-family: Consolas,monospace; font-size: 11px; }");
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

        // 告警：预算级别
        alertsLay_->removeWidget(emptyAlerts_);
        emptyAlerts_->setVisible(sum.alert_level == "none");
        if (sum.alert_level == "warn")
            alertsLay_->addWidget(new ui::AlertCard("warn", "⚠ Token 用量已达预算 80%，请控制消耗", this));
        else if (sum.alert_level == "critical")
            alertsLay_->addWidget(new ui::AlertCard("critical", "⛔ Token 用量已达预算 95%！", this));
        else if (sum.alert_level == "over")
            alertsLay_->addWidget(new ui::AlertCard("critical", "🚫 Token 用量已超出本周预算！", this));
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

    // 告警：未解决错误（分级卡片）
    std::vector<zp::ErrorReport> openErrors;
    if (platform_.errorList("open", "", 10, openErrors, err)) {
        for (auto* w : alertCards_) {
            alertsLay_->removeWidget(w);
            w->deleteLater();
        }
        alertCards_.clear();
        bool hasOpen = !openErrors.empty();
        emptyAlerts_->setVisible(sum.alert_level == "none" && !hasOpen);
        for (const auto& e : openErrors) {
            auto* card = new ui::AlertCard(
                e.severity == "critical" ? "critical"
                                         : (e.severity == "warning" ? "warn" : "note"),
                QString("[%1] %2 — %3")
                    .arg(QString::fromStdString(e.severity))
                    .arg(QString::fromStdString(e.title))
                    .arg(QString::fromStdString(e.reporter)),
                this);
            alertCards_.push_back(card);
            alertsLay_->addWidget(card);
        }
    }

    // 事件流时间线（最近 15 条审计）
    std::vector<zp::AuditRecord> records;
    if (platform_.auditList("", "", "", 15, records, err)) {
        timeline_->clear();
        for (const auto& r : records) {
            auto* item = new QListWidgetItem(QString("%1  ▸ %2  %3 %4")
                                                 .arg(QString::fromStdString(r.created_at))
                                                 .arg(QString::fromStdString(r.actor))
                                                 .arg(QString::fromStdString(r.action))
                                                 .arg(QString::fromStdString(r.target)));
            timeline_->addItem(item);
        }
    }
}
