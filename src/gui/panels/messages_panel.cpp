#include "messages_panel.h"

#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollBar>

#include "../gui_util.h"
#include "../i18n.h"
#include "../widgets.h"

namespace {
QString kindChip(const QString& kind) {
    if (kind == "task") return "<span style='background:#7f1d1d;color:#fecaca;"
                               "border-radius:4px;padding:1px 6px;'>任务</span>";
    if (kind == "question") return "<span style='background:#78350f;color:#fde68a;"
                                   "border-radius:4px;padding:1px 6px;'>提问</span>";
    return "<span style='background:#164e63;color:#a5f3fc;"
           "border-radius:4px;padding:1px 6px;'>留言</span>";
}
QString statusChip(const QString& s) {
    QString color = s == "done" ? "#22c55e"
                                 : (s == "declined" ? "#ef4444"
                                                    : (s == "accepted" ? "#0ea5e9" : "#eab308"));
    return QString("<span style='color:%1;'>● %2</span>").arg(color).arg(s.toHtmlEscaped());
}
// 发送者头像圈：取首字符，颜色由名字哈希决定（固定 6 色板）
QString avatar(const QString& sender) {
    static const char* kPalettes[] = {
        "#0ea5e9", "#22c55e", "#f59e0b", "#a78bfa", "#f472b6", "#34d399"};
    quint32 h = 0;
    for (QChar c : sender) h = h * 31 + c.unicode();
    QString initial = sender.left(1).toUpper().toHtmlEscaped();
    return QString("<span style='display:inline-block; min-width:18px; text-align:center;"
                   " background:%1; color:#101010; font-weight:700; border-radius:9px;"
                   " padding:1px 0;'>%2</span>")
        .arg(kPalettes[h % 6], initial);
}
}  // namespace

MessagesPanel::MessagesPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    buildHeader(layout, "Agent 交流", "Messaging",
                "留言 / 提问 / 指派任务，异步流转，不要求同时在线",
                "Notes, questions and tasks with async state machine");

    auto* toolbar = new QHBoxLayout;
    kindCombo_ = new QComboBox(this);
    kindCombo_->addItems({"全部", "note", "question", "task"});
    statusCombo_ = new QComboBox(this);
    statusCombo_->addItems({"全部", "unread", "read", "pending", "accepted", "done", "declined"});
    auto* composeBtn = new QPushButton(i18n::trs("＋ 发消息 / 指派任务", "＋ Message / Assign Task"), this);
    composeBtn->setObjectName("primary");
    toolbar->addWidget(new QLabel(i18n::trs("类型:", "Kind:"), this));
    toolbar->addWidget(kindCombo_);
    toolbar->addWidget(new QLabel(i18n::trs("状态:", "Status:"), this));
    toolbar->addWidget(statusCombo_);
    toolbar->addStretch(1);
    toolbar->addWidget(composeBtn);
    layout->addLayout(toolbar);
    connect(kindCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(statusCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(composeBtn, &QPushButton::clicked, this, &MessagesPanel::onCompose);

    // 对话流（点击气泡选中，锚点携带消息 uuid）
    chat_ = new QTextBrowser(this);
    chat_->setOpenLinks(false);
    layout->addWidget(chat_, 1);
    connect(chat_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        selectedUuid_ = url.toString().toStdString();
        updateActions();
    });

    // 操作条
    infoLabel_ = new QLabel(this);
    infoLabel_->setStyleSheet("color:#9ca3af; font-size:11px;");
    auto* brow = new QHBoxLayout;
    replyBtn_ = new QPushButton(i18n::trs("回复", "Reply"), this);
    readBtn_ = new QPushButton(i18n::trs("标记已读", "Mark Read"), this);
    acceptBtn_ = new QPushButton(i18n::trs("接受任务", "Accept"), this);
    doneBtn_ = new QPushButton(i18n::trs("任务完成", "Done"), this);
    declineBtn_ = new QPushButton(i18n::trs("拒绝任务", "Decline"), this);
    brow->addWidget(infoLabel_, 1);
    brow->addWidget(replyBtn_);
    brow->addWidget(readBtn_);
    brow->addWidget(acceptBtn_);
    brow->addWidget(doneBtn_);
    brow->addWidget(declineBtn_);
    layout->addLayout(brow);

    connect(replyBtn_, &QPushButton::clicked, this, &MessagesPanel::onReply);
    connect(readBtn_, &QPushButton::clicked, this, [this] { onStatus("read"); });
    connect(acceptBtn_, &QPushButton::clicked, this, [this] { onStatus("accepted"); });
    connect(doneBtn_, &QPushButton::clicked, this, [this] { onStatus("done"); });
    connect(declineBtn_, &QPushButton::clicked, this, [this] { onStatus("declined"); });
    updateActions();
}

void MessagesPanel::refresh() {
    std::string kind, status;
    if (kindCombo_->currentIndex() > 0) kind = kindCombo_->currentText().toStdString();
    if (statusCombo_->currentIndex() > 0) status = statusCombo_->currentText().toStdString();

    std::string err;
    platform_.messageList("", kind, status, "", 200, messages_, err);
    renderChat();
}

void MessagesPanel::renderChat() {
    // 3s 刷新重建 HTML 会把滚动条归零：先记住位置，渲染后恢复
    int scrollPos = chat_->verticalScrollBar()->value();
    bool atBottom = chat_->verticalScrollBar()->value() >=
                    chat_->verticalScrollBar()->maximum() - 4;
    QString html;
    for (const auto& m : messages_) {
        bool selected = m.uuid == selectedUuid_;
        QString recipient =
            m.recipient.empty() ? "全员" : QString::fromStdString(m.recipient);
        // 气泡：选中描边高亮；task/question/note 用色区分；头部带头像圈
        html += QString(
                    "<a name='%1'></a>"
                    "<div style='margin:6px 4px;'>"
                    "<span style='color:#9ca3af; font-size:10px; font-family:Consolas;'>%2</span> %7 "
                    "<span style='color:#e5e5e5; font-size:11px;'>%3</span>"
                    " <span style='color:#9ca3af;'>→ %4</span> %5 %6"
                    "<div style='%9'>"
                    "<b>%8</b><br>%10"
                    "</div></div>")
                    .arg(QString::fromStdString(m.uuid))
                    .arg(QString::fromStdString(m.created_at))
                    .arg(QString::fromStdString(m.sender).toHtmlEscaped())
                    .arg(recipient.toHtmlEscaped())
                    .arg(kindChip(QString::fromStdString(m.kind)))
                    .arg(statusChip(QString::fromStdString(m.status)))
                    .arg(avatar(QString::fromStdString(m.sender)))
                    .arg(QString::fromStdString(m.subject).toHtmlEscaped())
                    .arg(selected
                             ? "background:#2d2d3d; border:1px solid #0ea5e9; border-radius:8px; padding:8px 12px;"
                             : "background:#2d2d2d; border:1px solid #3f3f46; border-radius:8px; padding:8px 12px;")
                    .arg(QString::fromStdString(m.body).toHtmlEscaped()
                             .replace("\n", "<br>")
                             .left(500));
    }
    chat_->setHtml(html.isEmpty()
                       ? i18n::trs("<div style='color:#9ca3af; text-align:center;'>暂无消息，点击右上角发起交流</div>",
                       "<div style='color:#9ca3af; text-align:center;'>No messages yet — start a conversation</div>")
                       : html);
    // 恢复滚动位置；原本就在底部（阅读最新消息）则保持贴底
    QScrollBar* bar = chat_->verticalScrollBar();
    if (atBottom)
        bar->setValue(bar->maximum());
    else
        bar->setValue(qMin(scrollPos, bar->maximum()));
    // 选中气泡被重建后滚回可视区
    if (!selectedUuid_.empty()) chat_->scrollToAnchor(QString::fromStdString(selectedUuid_));
    updateActions();
}

void MessagesPanel::updateActions() {
    bool has = !selectedUuid_.empty();
    const zp::Message* cur = nullptr;
    for (const auto& m : messages_)
        if (m.uuid == selectedUuid_) cur = &m;
    if (has && cur) {
        infoLabel_->setText(QString("%1: %2 (%3 · %4)")
                                .arg(i18n::trs("已选中", "Selected"))
                                .arg(QString::fromStdString(cur->subject).toHtmlEscaped())
                                .arg(QString::fromStdString(cur->kind))
                                .arg(QString::fromStdString(cur->status)));
    } else {
        infoLabel_->setText(i18n::trs("点击消息气泡以选中（可回复 / 流转状态）",
                                      "Click a message bubble to select (reply / change status)"));
    }
    replyBtn_->setEnabled(has);
    readBtn_->setEnabled(has && cur && cur->status == "unread" && cur->kind != "task");
    acceptBtn_->setEnabled(has && cur && cur->kind == "task" && cur->status == "pending");
    doneBtn_->setEnabled(has && cur && cur->kind == "task" && cur->status == "accepted");
    declineBtn_->setEnabled(has && cur && cur->kind == "task" && cur->status == "pending");
}

void MessagesPanel::onCompose() {
    QDialog dlg(this);
    dlg.setWindowTitle(i18n::trs("发消息 / 指派任务", "Send Message / Assign Task"));
    auto* form = new QFormLayout(&dlg);
    auto* kind = new QComboBox(&dlg);
    kind->addItems({"note", "question", "task"});
    auto* recipient = new QComboBox(&dlg);
    recipient->addItem(i18n::trs("（广播给所有 Agent）", "(broadcast to all agents)"), "");
    std::vector<zp::AgentInfo> agents;
    std::string err;
    if (platform_.listAgents(agents, err))
        for (const auto& a : agents)
            recipient->addItem(QString::fromStdString(a.name), QString::fromStdString(a.name));
    auto* subject = new QLineEdit(&dlg);
    auto* body = new QPlainTextEdit(&dlg);
    form->addRow(i18n::trs("类型", "Kind"), kind);
    form->addRow(i18n::trs("接收者", "Recipient"), recipient);
    form->addRow(i18n::trs("主题", "Subject"), subject);
    form->addRow(i18n::trs("内容", "Body"), body);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    zp::Message out;
    if (!platform_.messageSend(kind->currentText().toStdString(), "user",
                               recipient->currentData().toString().toStdString(),
                               subject->text().trimmed().toStdString(),
                               body->toPlainText().toStdString(), out, err)) {
        ui::Toast::show(this, QString("发送失败: %1").arg(QString::fromStdString(err)), false);
        return;
    }
    ui::Toast::show(this, i18n::trs("消息已发送 ✓", "Message sent ✓"));
    refresh();
}

void MessagesPanel::onReply() {
    const zp::Message* cur = nullptr;
    for (const auto& m : messages_)
        if (m.uuid == selectedUuid_) cur = &m;
    if (!cur) return;
    QDialog dlg(this);
    dlg.setWindowTitle(QString("回复: %1").arg(QString::fromStdString(cur->subject)));
    auto* l = new QVBoxLayout(&dlg);
    auto* body = new QPlainTextEdit(&dlg);
    l->addWidget(body);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    l->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    zp::Message out;
    std::string err;
    if (!platform_.messageReply("user", cur->uuid, body->toPlainText().toStdString(), out, err)) {
        ui::Toast::show(this, QString("回复失败: %1").arg(QString::fromStdString(err)), false);
        return;
    }
    ui::Toast::show(this, i18n::trs("回复已发送 ✓", "Reply sent ✓"));
    refresh();
}

void MessagesPanel::onStatus(const QString& status) {
    const zp::Message* cur = nullptr;
    for (const auto& m : messages_)
        if (m.uuid == selectedUuid_) cur = &m;
    if (!cur) return;
    zp::Message out;
    std::string err;
    if (!platform_.messageSetStatus("user", cur->uuid, status.toStdString(), out, err)) {
        ui::Toast::show(this, QString("状态变更失败: %1").arg(QString::fromStdString(err)), false);
        return;
    }
    ui::Toast::show(this, QString("状态已更新为 %1 ✓").arg(status));
    refresh();
}
