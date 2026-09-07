#pragma once
// 操作日志：时间线视图（按日期分组），支持按日期与 Agent 筛选。
#include <QComboBox>
#include <QDateEdit>
#include <QLabel>
#include <QTreeWidget>
#include <vector>

#include "panel_base.h"

class LogsPanel : public PanelBase {
    Q_OBJECT
public:
    explicit LogsPanel(zp::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;

private:
    QComboBox* agentCombo_ = nullptr;
    QDateEdit* sinceEdit_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    std::vector<zp::AuditRecord> records_;
};
