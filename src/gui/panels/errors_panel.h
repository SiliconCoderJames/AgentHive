#pragma once
// 错误报告面板：浏览 / 筛选错误，查看堆栈与解决记录，登记解决说明。
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QLabel>

#include <vector>

#include "panel_base.h"

class ErrorsPanel : public PanelBase {
    Q_OBJECT
public:
    explicit ErrorsPanel(zp::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;
    void focusFilter() override;

private slots:
    void onResolve();

private:
    QComboBox* statusCombo_ = nullptr;
    QComboBox* severityCombo_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextBrowser* detail_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QPushButton* resolveBtn_ = nullptr;
    std::vector<zp::ErrorReport> errors_;
};
