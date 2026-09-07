#include "skills_panel.h"

#include <map>
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
    table_ = new QTableWidget(0, 7, splitter);
    table_->setHorizontalHeaderLabels({"名称", "显示名", "分类", "提供者", "版本", "状态", "使用热度"});
    table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    polishTable(table_);
    table_->resizeColumnsToContents();
    attachTableContextMenu(table_);
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

    // 使用热度：统计每个技能的调用次数
    std::vector<zp::SkillInvocation> allInv;
    platform_.skillInvocations("", 10000, allInv, err);
    std::map<std::string, int> heat;
    for (const auto& i : allInv) ++heat[i.skill_name];

    // 刷新前记住选中技能，重建后恢复选中，避免 3s 自动刷新打断浏览
    QString prevSelected;
    if (auto* cur = table_->item(table_->currentRow(), 0)) prevSelected = cur->text();

    table_->setRowCount(static_cast<int>(skills_.size()));
    int restoreRow = -1;
    for (size_t i = 0; i < skills_.size(); ++i) {
        const auto& s = skills_[i];
        int h = heat.count(s.name) ? heat[s.name] : 0;
        QString heatStr = h == 0 ? "—"
                                 : (h >= 10 ? QString("🔥 %1").arg(h) : QString::number(h));
        setRow(table_, static_cast<int>(i),
               {QString::fromStdString(s.name), QString::fromStdString(s.display_name),
                QString::fromStdString(s.category), QString::fromStdString(s.owner_agent),
                QString::number(s.version), QString::fromStdString(s.status), heatStr});
        if (!prevSelected.isEmpty() && prevSelected == QString::fromStdString(s.name))
            restoreRow = static_cast<int>(i);
    }
    table_->resizeColumnsToContents();  // 按实际内容重算列宽，避免截断
    if (restoreRow >= 0) {
        table_->selectRow(restoreRow);
        onSelectSkill(restoreRow);
    } else if (!skills_.empty()) {
        table_->selectRow(0);
        onSelectSkill(0);
    } else {
        // 空状态提示
        detail_->setHtml(
            "<div style='color:#9ca3af; text-align:center; margin-top:48px;'>"
            "暂无注册技能<br><br>点击右上角「＋ 注册技能」，或让 Agent 通过 "
            "<span style='font-family:Consolas;'>POST /api/skills</span> 注册（先注册后调用）</div>");
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
