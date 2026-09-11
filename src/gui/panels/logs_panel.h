#pragma once
// 操作日志：时间线视图（按日期分组），支持按日期与 Agent 筛选。
#include <QComboBox>
#include <QDateEdit>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <vector>

#include "panel_base.h"

class LogsPanel : public PanelBase {
    Q_OBJECT
public:
    explicit LogsPanel(ah::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;
    void focusFilter() override;

private:
    QComboBox* agentCombo_ = nullptr;
    QDateEdit* sinceEdit_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    std::vector<ah::AuditRecord> records_;
};
