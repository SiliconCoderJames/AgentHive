#pragma once
// Agent 交流面板：对话流视图（气泡 + 发送者标识 + 时间戳），
// 点击消息选中后可回复 / 流转状态，双击气泡直接回复。
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <vector>

#include "panel_base.h"

class MessagesPanel : public PanelBase {
    Q_OBJECT
public:
    explicit MessagesPanel(zp::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;
    bool eventFilter(QObject* obj, QEvent* e) override;  // 双击气泡 = 回复

private slots:
    void onCompose();
    void onReply();
    void onStatus(const QString& status);

private:
    void renderChat();
    void updateActions();

    QComboBox* kindCombo_ = nullptr;
    QComboBox* statusCombo_ = nullptr;
    QTextBrowser* chat_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QPushButton* replyBtn_ = nullptr;
    QPushButton* readBtn_ = nullptr;
    QPushButton* acceptBtn_ = nullptr;
    QPushButton* doneBtn_ = nullptr;
    QPushButton* declineBtn_ = nullptr;
    std::vector<zp::Message> messages_;
    std::string selectedUuid_;
};
