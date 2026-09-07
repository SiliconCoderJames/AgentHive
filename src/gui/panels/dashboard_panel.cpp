#include "dashboard_panel.h"

#include <algorithm>

#include <QGroupBox>
#include <QHeaderView>
#include <QVBoxLayout>

#include "../gui_util.h"

DashboardPanel::DashboardPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);

    // ---- Token 预算 ----
    auto* budgetBox = new QGroupBox("本周 Token 用量（预算 1000 万）", this);
    auto* bl = new QVBoxLayout(budgetBox);
    budgetBar_ = new QProgressBar(budgetBox);
    budgetBar_->setMinimum(0);
    budgetBar_->setTextVisible(true);
    budgetLabel_ = new QLabel(budgetBox);
    bl->addWidget(budgetBar_);
    bl->addWidget(budgetLabel_);
    layout->addWidget(budgetBox);

    // ---- Agent 状态 ----
    auto* agentsBox = new QGroupBox("Agent 状态", this);
    auto* al = new QVBoxLayout(agentsBox);
    agentsTable_ = new QTableWidget(0, 5, agentsBox);
    agentsTable_->setHorizontalHeaderLabels({"名称", "角色", "状态", "当前任务", "最后活跃"});
    agentsTable_->horizontalHeader()->setStretchLastSection(true);
    agentsTable_->verticalHeader()->setVisible(false);
    agentsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    al->addWidget(agentsTable_);
    layout->addWidget(agentsBox, 1);

    // ---- 各 Agent 用量 ----
    auto* usageBox = new QGroupBox("各 Agent 本周用量", this);
    auto* ul = new QVBoxLayout(usageBox);
    usageTable_ = new QTableWidget(0, 2, usageBox);
    usageTable_->setHorizontalHeaderLabels({"Agent", "Token 数"});
    usageTable_->horizontalHeader()->setStretchLastSection(true);
    usageTable_->verticalHeader()->setVisible(false);
    usageTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ul->addWidget(usageTable_);
    layout->addWidget(usageBox, 1);

    // ---- 告警 ----
    auto* alertsBox = new QGroupBox("告警与待处理错误", this);
    auto* wl = new QVBoxLayout(alertsBox);
    alertsList_ = new QListWidget(alertsBox);
    wl->addWidget(alertsList_);
    layout->addWidget(alertsBox, 1);
}

void DashboardPanel::refresh() {
    std::string err;

    // 预算进度
    zp::UsageSummary sum;
    if (platform_.usageSummary(sum, err)) {
        budgetBar_->setMaximum(static_cast<int>(sum.budget > 0 ? sum.budget : 1));
        budgetBar_->setValue(static_cast<int>(std::min<int64_t>(sum.total_tokens, sum.budget)));
        QString style = sum.alert_level == "none"
                            ? "QProgressBar::chunk { background: #2e7d32; }"
                            : (sum.alert_level == "warn"
                                   ? "QProgressBar::chunk { background: #ef6c00; }"
                                   : "QProgressBar::chunk { background: #c62828; }");
        budgetBar_->setStyleSheet(style);
        double pct = sum.budget > 0 ? 100.0 * sum.total_tokens / sum.budget : 0.0;
        budgetLabel_->setText(QString("已用 %1 / %2（%3%）· 剩余 %4 · 状态: %5")
                                  .arg(formatNum(sum.total_tokens))
                                  .arg(formatNum(sum.budget))
                                  .arg(pct, 0, 'f', 2)
                                  .arg(formatNum(sum.budget - sum.total_tokens))
                                  .arg(QString::fromStdString(sum.alert_level)));
    }

    // Agent 状态表
    std::vector<zp::AgentInfo> agents;
    if (platform_.listAgents(agents, err)) {
        agentsTable_->setRowCount(static_cast<int>(agents.size()));
        for (size_t i = 0; i < agents.size(); ++i) {
            const auto& a = agents[i];
            setRow(agentsTable_, static_cast<int>(i),
                   {QString::fromStdString(a.name), QString::fromStdString(a.role),
                    QString::fromStdString(a.status), QString::fromStdString(a.current_task),
                    QString::fromStdString(a.last_seen_at)});
        }
    }

    // 各 Agent 用量
    usageTable_->setRowCount(static_cast<int>(sum.per_agent.size()));
    for (size_t i = 0; i < sum.per_agent.size(); ++i) {
        setRow(usageTable_, static_cast<int>(i),
               {QString::fromStdString(sum.per_agent[i].first),
                formatNum(sum.per_agent[i].second)});
    }

    // 告警
    alertsList_->clear();
    if (sum.alert_level == "warn")
        alertsList_->addItem("⚠ Token 用量已达预算 80%，请控制消耗");
    else if (sum.alert_level == "critical")
        alertsList_->addItem("⛔ Token 用量已达预算 95%，即将耗尽！");
    else if (sum.alert_level == "over")
        alertsList_->addItem("🚫 Token 用量已超出本周预算！");

    std::vector<zp::ErrorReport> openErrors;
    if (platform_.errorList("open", "", 10, openErrors, err)) {
        for (const auto& e : openErrors)
            alertsList_->addItem(QString("[%1] %2（来自 %3）")
                                     .arg(QString::fromStdString(e.severity))
                                     .arg(QString::fromStdString(e.title))
                                     .arg(QString::fromStdString(e.reporter)));
    }
}
