#pragma once
// 用户记忆面板：分区块浏览、查看 / 编辑画像（编辑生成新版本）、历史版本。
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>

#include <vector>

#include "panel_base.h"

class MemoryPanel : public PanelBase {
    Q_OBJECT
public:
    explicit MemoryPanel(zp::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;

private slots:
    void onEdit();
    void onShowHistory();

private:
    QComboBox* sectionCombo_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextBrowser* valueView_ = nullptr;
    std::vector<zp::MemoryEntry> entries_;
    int selectedRow() const { return table_->currentRow(); }
};
