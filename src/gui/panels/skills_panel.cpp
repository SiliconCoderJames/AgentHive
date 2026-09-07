#include "skills_panel.h"

#include <set>

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

SkillsPanel::SkillsPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);

    auto* toolbar = new QHBoxLayout;
    categoryCombo_ = new QComboBox(this);
    ownerCombo_ = new QComboBox(this);
    auto* refreshBtn = new QPushButton("刷新", this);
    auto* registerBtn = new QPushButton("＋ 注册技能", this);
    toolbar->addWidget(new QLabel("分类:", this));
    toolbar->addWidget(categoryCombo_);
    toolbar->addWidget(new QLabel("提供者:", this));
    toolbar->addWidget(ownerCombo_);
    toolbar->addWidget(refreshBtn);
    toolbar->addStretch(1);
    toolbar->addWidget(registerBtn);
    layout->addLayout(toolbar);
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
    connect(categoryCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(ownerCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(registerBtn, &QPushButton::clicked, this, &SkillsPanel::onRegister);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    table_ = new QTableWidget(0, 6, splitter);
    table_->setHorizontalHeaderLabels({"名称", "显示名", "分类", "提供者", "版本", "状态"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    splitter->addWidget(table_);
    detail_ = new QTextBrowser(splitter);
    splitter->addWidget(detail_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) { onSelectSkill(row); });
}

void SkillsPanel::refresh() {
    // 填充筛选下拉（保留当前选择）
    std::string err;
    std::vector<zp::SkillInfo> all;
    platform_.skillList("", "", all, err);
    std::set<std::string> categories, owners;
    for (const auto& s : all) {
        if (!s.category.empty()) categories.insert(s.category);
        if (!s.owner_agent.empty()) owners.insert(s.owner_agent);
    }
    QString curCat = categoryCombo_->currentText();
    QString curOwner = ownerCombo_->currentText();
    categoryCombo_->blockSignals(true);
    ownerCombo_->blockSignals(true);
    categoryCombo_->clear();
    ownerCombo_->clear();
    categoryCombo_->addItem("全部");
    ownerCombo_->addItem("全部");
    for (const auto& c : categories) categoryCombo_->addItem(QString::fromStdString(c));
    for (const auto& o : owners) ownerCombo_->addItem(QString::fromStdString(o));
    categoryCombo_->setCurrentText(curCat);
    ownerCombo_->setCurrentText(curOwner);
    categoryCombo_->blockSignals(false);
    ownerCombo_->blockSignals(false);

    std::string cat = categoryCombo_->currentText() == "全部" ? "" : categoryCombo_->currentText().toStdString();
    std::string owner = ownerCombo_->currentText() == "全部" ? "" : ownerCombo_->currentText().toStdString();
    platform_.skillList(cat, owner, skills_, err);

    table_->setRowCount(static_cast<int>(skills_.size()));
    for (size_t i = 0; i < skills_.size(); ++i) {
        const auto& s = skills_[i];
        setRow(table_, static_cast<int>(i),
               {QString::fromStdString(s.name), QString::fromStdString(s.display_name),
                QString::fromStdString(s.category), QString::fromStdString(s.owner_agent),
                QString::number(s.version), QString::fromStdString(s.status)});
    }
    if (!skills_.empty()) {
        table_->selectRow(0);
        onSelectSkill(0);
    } else {
        detail_->clear();
    }
}

void SkillsPanel::onSelectSkill(int row) {
    if (row < 0 || row >= static_cast<int>(skills_.size())) return;
    const auto& s = skills_[static_cast<size_t>(row)];
    detail_->setHtml(
        QString("<h3>%1</h3><p><i>%2</i></p>"
                "<p><b>提供者:</b> %3 · <b>分类:</b> %4 · <b>版本:</b> v%5 · <b>状态:</b> %6</p>"
                "<p><b>更新:</b> %7</p>"
                "<h4>参数 Schema</h4><pre>%8</pre>")
            .arg(QString::fromStdString(s.name).toHtmlEscaped())
            .arg(QString::fromStdString(s.description).toHtmlEscaped())
            .arg(QString::fromStdString(s.owner_agent))
            .arg(QString::fromStdString(s.category))
            .arg(s.version)
            .arg(QString::fromStdString(s.status))
            .arg(QString::fromStdString(s.updated_at))
            .arg(QString::fromStdString(s.param_schema).toHtmlEscaped()));
}

void SkillsPanel::onRegister() {
    QDialog dlg(this);
    dlg.setWindowTitle("注册新技能（先注册后调用）");
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(&dlg);
    auto* display = new QLineEdit(&dlg);
    auto* desc = new QPlainTextEdit(&dlg);
    auto* category = new QLineEdit(&dlg);
    auto* schema = new QPlainTextEdit(&dlg);
    schema->setPlainText("{}");
    form->addRow("技能名（唯一）", name);
    form->addRow("显示名", display);
    form->addRow("描述", desc);
    form->addRow("分类", category);
    form->addRow("参数 Schema (JSON)", schema);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    std::string err;
    zp::SkillInfo out;
    if (!platform_.skillRegister("user", name->text().trimmed().toStdString(),
                                 display->text().trimmed().toStdString(),
                                 desc->toPlainText().toStdString(),
                                 category->text().trimmed().toStdString(),
                                 schema->toPlainText().toStdString(), out, err)) {
        QMessageBox::warning(this, "注册失败", QString::fromStdString(err));
        return;
    }
    refresh();
}
