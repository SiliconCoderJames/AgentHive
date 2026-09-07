#include "messages_panel.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>

#include "../gui_util.h"

MessagesPanel::MessagesPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);

    auto* toolbar = new QHBoxLayout;
    kindCombo_ = new QComboBox(this);
    kindCombo_->addItems({"全部", "note 留言", "question 提问", "task 任务"});
    statusCombo_ = new QComboBox(this);
    statusCombo_->addItems(
        {"全部", "unread", "read", "pending", "accepted", "done", "declined"});
    auto* composeBtn = new QPushButton("＋ 发消息", this);
    toolbar->addWidget(new QLabel("类型:", this));
    toolbar->addWidget(kindCombo_);
    toolbar->addWidget(new QLabel("状态:", this));
    toolbar->addWidget(statusCombo_);
    toolbar->addStretch(1);
    toolbar->addWidget(composeBtn);
    layout->addLayout(toolbar);
    connect(kindCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(statusCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(composeBtn, &QPushButton::clicked, this, &MessagesPanel::onCompose);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    table_ = new QTableWidget(0, 6, splitter);
    table_->setHorizontalHeaderLabels({"类型", "发送者", "接收者", "主题", "状态", "时间"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    splitter->addWidget(table_);

    auto* right = new QWidget(splitter);
    auto* rl = new QVBoxLayout(right);
    infoLabel_ = new QLabel(right);
    rl->addWidget(infoLabel_);
    detail_ = new QTextBrowser(right);
    rl->addWidget(detail_, 1);
    auto* brow = new QHBoxLayout;
    auto* replyBtn = new QPushButton("回复", right);
    auto* readBtn = new QPushButton("标记已读", right);
    auto* acceptBtn = new QPushButton("接受任务", right);
    auto* doneBtn = new QPushButton("任务完成", right);
    auto* declineBtn = new QPushButton("拒绝任务", right);
    brow->addWidget(replyBtn);
    brow->addWidget(readBtn);
    brow->addStretch(1);
    brow->addWidget(acceptBtn);
    brow->addWidget(doneBtn);
    brow->addWidget(declineBtn);
    rl->addLayout(brow);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    connect(replyBtn, &QPushButton::clicked, this, &MessagesPanel::onReply);
    connect(readBtn, &QPushButton::clicked, this, [this] { onStatus("read"); });
    connect(acceptBtn, &QPushButton::clicked, this, [this] { onStatus("accepted"); });
    connect(doneBtn, &QPushButton::clicked, this, [this] { onStatus("done"); });
    connect(declineBtn, &QPushButton::clicked, this, [this] { onStatus("declined"); });

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= static_cast<int>(messages_.size())) return;
        const auto& m = messages_[static_cast<size_t>(row)];
        detail_->setPlainText(QString::fromStdString(m.body));
        infoLabel_->setText(QString("<b>%1</b> · 发送者: %2 · 接收者: %3 · 状态: %4")
                                .arg(QString::fromStdString(m.subject).toHtmlEscaped())
                                .arg(QString::fromStdString(m.sender))
                                .arg(m.recipient.empty() ? "全部" : QString::fromStdString(m.recipient))
                                .arg(QString::fromStdString(m.status)));
    });
}

void MessagesPanel::refresh() {
    std::string kind, status;
    if (kindCombo_->currentIndex() > 0)
        kind = kindCombo_->currentText().split(' ').first().toStdString();
    if (statusCombo_->currentIndex() > 0) status = statusCombo_->currentText().toStdString();

    std::string err;
    platform_.messageList("", kind, status, "", 200, messages_, err);

    table_->setRowCount(static_cast<int>(messages_.size()));
    for (size_t i = 0; i < messages_.size(); ++i) {
        const auto& m = messages_[i];
        setRow(table_, static_cast<int>(i),
               {QString::fromStdString(m.kind), QString::fromStdString(m.sender),
                m.recipient.empty() ? "全部" : QString::fromStdString(m.recipient),
                QString::fromStdString(m.subject), QString::fromStdString(m.status),
                QString::fromStdString(m.created_at)});
    }
}

void MessagesPanel::onCompose() {
    QDialog dlg(this);
    dlg.setWindowTitle("发消息 / 指派任务");
    auto* form = new QFormLayout(&dlg);
    auto* kind = new QComboBox(&dlg);
    kind->addItems({"note", "question", "task"});
    auto* recipient = new QComboBox(&dlg);
    recipient->addItem("（广播给所有 Agent）", "");
    std::vector<zp::AgentInfo> agents;
    std::string err;
    if (platform_.listAgents(agents, err))
        for (const auto& a : agents)
            recipient->addItem(QString::fromStdString(a.name), QString::fromStdString(a.name));
    auto* subject = new QLineEdit(&dlg);
    auto* body = new QPlainTextEdit(&dlg);
    form->addRow("类型", kind);
    form->addRow("接收者", recipient);
    form->addRow("主题", subject);
    form->addRow("内容", body);
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
        QMessageBox::warning(this, "发送失败", QString::fromStdString(err));
        return;
    }
    refresh();
}

void MessagesPanel::onReply() {
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(messages_.size())) {
        QMessageBox::information(this, "回复", "请先选择一条消息");
        return;
    }
    const auto& m = messages_[static_cast<size_t>(row)];
    QDialog dlg(this);
    dlg.setWindowTitle(QString("回复: %1").arg(QString::fromStdString(m.subject)));
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
    if (!platform_.messageReply("user", m.uuid, body->toPlainText().toStdString(), out, err)) {
        QMessageBox::warning(this, "回复失败", QString::fromStdString(err));
        return;
    }
    refresh();
}

void MessagesPanel::onStatus(const QString& status) {
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(messages_.size())) return;
    const auto& m = messages_[static_cast<size_t>(row)];
    zp::Message out;
    std::string err;
    if (!platform_.messageSetStatus("user", m.uuid, status.toStdString(), out, err)) {
        QMessageBox::warning(this, "状态变更失败", QString::fromStdString(err));
        return;
    }
    refresh();
}
