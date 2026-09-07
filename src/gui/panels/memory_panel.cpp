#include "memory_panel.h"

#include <algorithm>
#include <map>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>

#include "../gui_util.h"
#include "../widgets.h"

namespace {
// 五大区块（显示名, 存储名）
const std::vector<std::pair<QString, QString>> kSections{
    {"项目档案", "project"},   {"决策日志", "decision"}, {"偏好记录", "preference"},
    {"设备环境", "environment"}, {"工作习惯", "work_style"},
};
}  // namespace

MemoryPanel::MemoryPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    buildHeader(layout, "用户记忆", "项目档案 · 决策日志 · 偏好记录 · 设备环境 · 工作习惯，所有 Agent 共享");

    auto* toolbar = new QHBoxLayout;
    headerLabel_ = new QLabel(this);
    headerLabel_->setStyleSheet("font-size:13px; color:#9ca3af;");
    editBtn_ = new QPushButton("编辑 / 新增（生成新版本）", this);
    editBtn_->setObjectName("primary");
    historyBtn_ = new QPushButton("查看历史版本", this);
    toolbar->addWidget(headerLabel_);
    toolbar->addStretch(1);
    toolbar->addWidget(historyBtn_);
    toolbar->addWidget(editBtn_);
    layout->addLayout(toolbar);
    connect(editBtn_, &QPushButton::clicked, this, &MemoryPanel::onEdit);
    connect(historyBtn_, &QPushButton::clicked, this, &MemoryPanel::onShowHistory);

    // 折叠卡片滚动区
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* holder = new QWidget(scroll);
    sectionsLay_ = new QVBoxLayout(holder);
    sectionsLay_->setContentsMargins(0, 0, 12, 0);
    sectionsLay_->setSpacing(10);
    sectionsLay_->addStretch(1);
    scroll->setWidget(holder);
    layout->addWidget(scroll, 1);
}

void MemoryPanel::refresh() {
    std::string err;
    platform_.memoryList("", entries_, err);

    // 头部：条目总数 + 最后更新时间
    QString lastUpdated = "—";
    for (const auto& m : entries_) {
        QString t = QString::fromStdString(m.created_at);
        if (t > lastUpdated || lastUpdated == "—") lastUpdated = t;
    }
    headerLabel_->setText(QString("记忆条目总数: %1 · 最后更新: %2")
                              .arg(formatNum(static_cast<qint64>(entries_.size())))
                              .arg(lastUpdated));

    // 重建五区块折叠卡片
    while (sectionsLay_->count() > 1) {  // 末尾是 stretch
        QLayoutItem* it = sectionsLay_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    for (const auto& [title, section] : kSections) {
        std::vector<zp::MemoryEntry> group;
        for (const auto& m : entries_)
            if (m.section == section.toStdString()) group.push_back(m);

        auto* content = new QWidget(this);
        auto* cl = new QVBoxLayout(content);
        cl->setContentsMargins(14, 10, 14, 12);
        cl->setSpacing(6);
        if (group.empty()) {
            auto* empty = new QLabel("该区块暂无记忆", content);
            empty->setStyleSheet("color:#9ca3af; font-size:11px;");
            cl->addWidget(empty);
        } else {
            for (const auto& m : group) {
                auto* row = new QLabel(
                    QString("<b style='color:#0ea5e9;'>%1</b>"
                            " <span style='color:#9ca3af; font-size:10px;'>v%2 · %3 · %4</span><br>%5")
                        .arg(QString::fromStdString(m.key).toHtmlEscaped())
                        .arg(m.version)
                        .arg(QString::fromStdString(m.author))
                        .arg(QString::fromStdString(m.created_at))
                        .arg(QString::fromStdString(m.value).toHtmlEscaped().left(200)),
                    content);
                row->setTextFormat(Qt::RichText);
                row->setWordWrap(true);
                cl->addWidget(row);
            }
        }
        cl->addStretch(1);
        sectionsLay_->insertWidget(sectionsLay_->count() - 1,
                                   new ui::SectionCard(title, content, this));
    }
}

void MemoryPanel::onEdit() {
    QDialog dlg(this);
    dlg.setWindowTitle("编辑用户记忆（保存后生成新版本，历史保留）");
    auto* form = new QFormLayout(&dlg);
    auto* section = new QComboBox(&dlg);
    for (const auto& [title, name] : kSections) section->addItem(title, name);
    auto* key = new QLineEdit(&dlg);
    auto* value = new QPlainTextEdit(&dlg);
    form->addRow("区块", section);
    form->addRow("键", key);
    form->addRow("值", value);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    std::string err;
    zp::MemoryEntry out;
    if (!platform_.memorySet("user", section->currentData().toString().toStdString(),
                             key->text().trimmed().toStdString(), value->toPlainText().toStdString(),
                             0, out, err)) {
        ui::Toast::show(this, QString("保存失败: %1").arg(QString::fromStdString(err)), false);
        return;
    }
    ui::Toast::show(this, QString("记忆已保存（v%1）✓").arg(out.version));
    refresh();
}

void MemoryPanel::onShowHistory() {
    QDialog dlg(this);
    dlg.setWindowTitle("按键查询历史版本");
    auto* form = new QFormLayout(&dlg);
    auto* section = new QComboBox(&dlg);
    for (const auto& [title, name] : kSections) section->addItem(title, name);
    auto* key = new QLineEdit(&dlg);
    form->addRow("区块", section);
    form->addRow("键", key);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    std::vector<zp::MemoryEntry> history;
    std::string err;
    if (!platform_.memoryHistory(section->currentData().toString().toStdString(),
                                 key->text().trimmed().toStdString(), history, err) ||
        history.empty()) {
        QMessageBox::information(this, "历史版本", "该键无记忆记录");
        return;
    }
    QDialog view(this);
    view.setWindowTitle(QString("历史版本 (%1 条)").arg(history.size()));
    auto* l = new QVBoxLayout(&view);
    auto* table = new QTableWidget(static_cast<int>(history.size()), 4, &view);
    table->setHorizontalHeaderLabels({"版本", "作者", "时间", "值"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (size_t i = 0; i < history.size(); ++i)
        setRow(table, static_cast<int>(i),
               {QString::number(history[i].version), QString::fromStdString(history[i].author),
                QString::fromStdString(history[i].created_at),
                QString::fromStdString(history[i].value)});
    l->addWidget(table);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &view);
    connect(close, &QDialogButtonBox::rejected, &view, &QDialog::reject);
    l->addWidget(close);
    view.resize(720, 400);
    view.exec();
}
