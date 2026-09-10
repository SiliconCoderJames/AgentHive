#pragma once
// 技能库面板：按 Agent / 分类筛选浏览、注册新技能、查看调用记录。
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>

#include <vector>

#include "panel_base.h"

class SkillsPanel : public PanelBase {
    Q_OBJECT
public:
    explicit SkillsPanel(zp::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;
    void focusFilter() override;

private slots:
    void onRegister();
    void onSelectSkill(int row);

private:
    QComboBox* categoryCombo_ = nullptr;
    QComboBox* ownerCombo_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextBrowser* detail_ = nullptr;
    std::vector<zp::SkillInfo> skills_;
};
