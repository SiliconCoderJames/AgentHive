#pragma once
// Agent 交流面板：留言 / 提问 / 指派任务的消息流、回复与任务状态流转。
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QLabel>

#include <vector>

#include "panel_base.h"

class MessagesPanel : public PanelBase {
    Q_OBJECT
public:
    explicit MessagesPanel(zp::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;

private slots:
    void onCompose();
    void onReply();
    void onStatus(const QString& status);

private:
    QComboBox* kindCombo_ = nullptr;
    QComboBox* statusCombo_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextBrowser* detail_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    std::vector<zp::Message> messages_;
};
