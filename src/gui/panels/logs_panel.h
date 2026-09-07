#pragma once
// 操作日志面板：按身份 / 动作筛选，完整可追溯。
#include <QLineEdit>
#include <QTableWidget>
#include <QLabel>
#include <QSpinBox>

#include <vector>

#include "panel_base.h"

class LogsPanel : public PanelBase {
    Q_OBJECT
public:
    explicit LogsPanel(zp::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;

private:
    QLineEdit* actorEdit_ = nullptr;
    QLineEdit* actionEdit_ = nullptr;
    QSpinBox* limitSpin_ = nullptr;
    QTableWidget* table_ = nullptr;
    std::vector<zp::AuditRecord> records_;
};
