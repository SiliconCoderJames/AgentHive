#include "memory_panel.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>

#include "../gui_util.h"

namespace {
const QStringList kSections{"project", "preference", "work_style", "decision", "environment"};
}  // namespace

MemoryPanel::MemoryPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);

    auto* toolbar = new QHBoxLayout;
    sectionCombo_ = new QComboBox(this);
    sectionCombo_->addItem("全部");
    sectionCombo_->addItems(kSections);
    auto* editBtn = new QPushButton("编辑 / 新增（生成新版本）", this);
    auto* historyBtn = new QPushButton("查看历史版本", this);
    toolbar->addWidget(new QLabel("区块:", this));
    toolbar->addWidget(sectionCombo_);
    toolbar->addStretch(1);
    toolbar->addWidget(historyBtn);
    toolbar->addWidget(editBtn);
    layout->addLayout(toolbar);
    connect(sectionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(editBtn, &QPushButton::clicked, this, &MemoryPanel::onEdit);
    connect(historyBtn, &QPushButton::clicked, this, &MemoryPanel::onShowHistory);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    table_ = new QTableWidget(0, 5, splitter);
    table_->setHorizontalHeaderLabels({"区块", "键", "作者", "版本", "更新时间"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    splitter->addWidget(table_);
    valueView_ = new QTextBrowser(splitter);
    splitter->addWidget(valueView_);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row >= 0 && row < static_cast<int>(entries_.size()))
            valueView_->setPlainText(QString::fromStdString(entries_[static_cast<size_t>(row)].value));
    });
}

void MemoryPanel::refresh() {
    std::string section = sectionCombo_->currentIndex() <= 0
                              ? ""
                              : sectionCombo_->currentText().toStdString();
    std::string err;
    platform_.memoryList(section, entries_, err);

    table_->setRowCount(static_cast<int>(entries_.size()));
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& m = entries_[i];
        setRow(table_, static_cast<int>(i),
               {QString::fromStdString(m.section), QString::fromStdString(m.key),
                QString::fromStdString(m.author), QString::number(m.version),
                QString::fromStdString(m.created_at)});
    }
    if (valueView_->toPlainText().isEmpty() && !entries_.empty()) {
        table_->selectRow(0);
        valueView_->setPlainText(QString::fromStdString(entries_[0].value));
    }
}

void MemoryPanel::onEdit() {
    QDialog dlg(this);
    dlg.setWindowTitle("编辑用户记忆（保存后生成新版本，历史保留）");
    auto* form = new QFormLayout(&dlg);
    auto* section = new QComboBox(&dlg);
    section->addItems(kSections);
    auto* key = new QLineEdit(&dlg);
    auto* value = new QPlainTextEdit(&dlg);

    int row = selectedRow();
    if (row >= 0 && row < static_cast<int>(entries_.size())) {
        section->setCurrentText(QString::fromStdString(entries_[static_cast<size_t>(row)].section));
        key->setText(QString::fromStdString(entries_[static_cast<size_t>(row)].key));
        value->setPlainText(QString::fromStdString(entries_[static_cast<size_t>(row)].value));
    }
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
    if (!platform_.memorySet("user", section->currentText().toStdString(), key->text().trimmed().toStdString(),
                             value->toPlainText().toStdString(), out, err)) {
        QMessageBox::warning(this, "保存失败", QString::fromStdString(err));
        return;
    }
    refresh();
}

void MemoryPanel::onShowHistory() {
    int row = selectedRow();
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        QMessageBox::information(this, "历史版本", "请先在左侧选择一条记忆");
        return;
    }
    const auto& cur = entries_[static_cast<size_t>(row)];
    std::vector<zp::MemoryEntry> history;
    std::string err;
    if (!platform_.memoryHistory(cur.section, cur.key, history, err)) {
        QMessageBox::warning(this, "读取失败", QString::fromStdString(err));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(QString("历史版本: %1 / %2")
                           .arg(QString::fromStdString(cur.section))
                           .arg(QString::fromStdString(cur.key)));
    auto* l = new QVBoxLayout(&dlg);
    auto* table = new QTableWidget(static_cast<int>(history.size()), 4, &dlg);
    table->setHorizontalHeaderLabels({"版本", "作者", "时间", "值"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (size_t i = 0; i < history.size(); ++i)
        setRow(table, static_cast<int>(i),
               {QString::number(history[i].version), QString::fromStdString(history[i].author),
                QString::fromStdString(history[i].created_at), QString::fromStdString(history[i].value)});
    l->addWidget(table);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    l->addWidget(buttons);
    dlg.resize(720, 400);
    dlg.exec();
}
