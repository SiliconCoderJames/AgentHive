#pragma once
// 总览面板：环形 Token 预算图 + 各 Agent 用量柱状图 + Agent 状态卡片网格
// + 底部事件流时间线 + 分级告警卡片。
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <vector>

#include "../widgets.h"
#include "panel_base.h"

class DashboardPanel : public PanelBase {
    Q_OBJECT
public:
    explicit DashboardPanel(zp::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;

private:
    ui::RingProgress* ring_ = nullptr;
    ui::HBarChart* usageChart_ = nullptr;
    QGridLayout* agentGrid_ = nullptr;
    QListWidget* timeline_ = nullptr;
    QVBoxLayout* alertsLay_ = nullptr;
    QLabel* emptyAlerts_ = nullptr;
    std::vector<ui::AgentCard*> agentCards_;
    std::vector<QWidget*> alertCards_;
    QString budgetAlertLevel_;
};
