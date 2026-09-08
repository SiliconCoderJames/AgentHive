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
#include "../i18n.h"
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
    buildHeader(layout, "用户记忆", "User Memory",
                "项目档案 · 决策日志 · 偏好记录 · 设备环境 · 工作习惯，所有 Agent 共享",
                "Project, decisions, preferences, environment and habits - shared by all agents");

    auto* toolbar = new QHBoxLayout;
    headerLabel_ = new QLabel(this);
    headerLabel_->setStyleSheet("font-size:13px; color:#9ca3af;");
    auto* editBtn = new QPushButton(i18n::trs("编辑 / 新增（生成新版本）", "Edit / Add (new version)"), this);
    editBtn->setObjectName("primary");
    auto* historyBtn = new QPushButton(i18n::trs("查看历史版本", "History"), this);
    toolbar->addWidget(headerLabel_);
    toolbar->addStretch(1);
    toolbar->addWidget(historyBtn);
    toolbar->addWidget(editBtn);
    layout->addLayout(toolbar);
    connect(editBtn, &QPushButton::clicked, this, &MemoryPanel::onEdit);
    connect(historyBtn, &QPushButton::clicked, this, &MemoryPanel::onShowHistory);

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
            empty->setText(i18n::trs("该区块暂无记忆", "No memories in this section yet")); empty->setStyleSheet("color:#9ca3af; font-size:11px;");
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
    dlg.setWindowTitle(i18n::trs("编辑用户记忆（保存后生成新版本，历史保留）", "Edit Memory (saved as a new version; history kept)"));
    auto* form = new QFormLayout(&dlg);
    auto* section = new QComboBox(&dlg);
    for (const auto& [title, name] : kSections) section->addItem(title, name);
    auto* key = new QLineEdit(&dlg);
    auto* value = new QPlainTextEdit(&dlg);
    form->addRow(i18n::trs("区块", "Section"), section);
    form->addRow(i18n::trs("键", "Key"), key);
    form->addRow(i18n::trs("值", "Value"), value);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    // 乐观并发：以当前最新版本为 base 保存，他人先写过则拒绝而非静默覆盖
    const std::string sectionName = section->currentData().toString().toStdString();
    const std::string keyName = key->text().trimmed().toStdString();
    int baseVersion = 0;
    std::vector<zp::MemoryEntry> existing;
    std::string probeErr;
    if (!keyName.empty() && platform_.memoryList(sectionName, existing, probeErr)) {
        for (const auto& m : existing)
            if (m.key == keyName) baseVersion = m.version;
    }

    std::string err;
    zp::MemoryEntry out;
    if (!platform_.memorySet("user", sectionName, keyName, value->toPlainText().toStdString(),
                             baseVersion, out, err)) {
        ui::Toast::show(this, QString("保存失败: %1").arg(QString::fromStdString(err)), false);
        return;
    }
    ui::Toast::show(this, QString("记忆已保存（v%1）✓").arg(out.version));
    refresh();
}

void MemoryPanel::onShowHistory() {
    QDialog dlg(this);
    dlg.setWindowTitle(i18n::trs("按键查询历史版本", "Query History by Key"));
    auto* form = new QFormLayout(&dlg);
    auto* section = new QComboBox(&dlg);
    for (const auto& [title, name] : kSections) section->addItem(title, name);
    auto* key = new QLineEdit(&dlg);
    form->addRow(i18n::trs("区块", "Section"), section);
    form->addRow(i18n::trs("键", "Key"), key);
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
        QMessageBox::information(this, i18n::trs("历史版本", "History"),
                            i18n::trs("该键无记忆记录", "No records for this key"));
        return;
    }
    QDialog view(this);
    view.setWindowTitle(i18n::trs("历史版本 (%1 条)", "History (%1 entries)").arg(history.size()));
    auto* l = new QVBoxLayout(&view);
    auto* table = new QTableWidget(static_cast<int>(history.size()), 4, &view);
    table->setHorizontalHeaderLabels({"版本", "作者", "时间", "值"});
    table->horizontalHeader()->setStretchLastSection(true);
    polishTable(table);
    for (size_t i = 0; i < history.size(); ++i) {
        // 列表按版本倒序：首行即最新版本，标注出来；值列截断显示，全文在悬停提示
        QString v = QString::fromStdString(history[i].value);
        QString shown = v.size() > 200 ? v.left(200) + "…" : v;
        QString ver = QString("v%1").arg(history[i].version);
        if (i == 0) ver += "（最新）";
        setRow(table, static_cast<int>(i),
               {ver, QString::fromStdString(history[i].author),
                QString::fromStdString(history[i].created_at), shown});
    }
    table->resizeColumnToContents(0);
    table->resizeColumnToContents(1);
    table->resizeColumnToContents(2);
    l->addWidget(table);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &view);
    connect(close, &QDialogButtonBox::rejected, &view, &QDialog::reject);
    l->addWidget(close);
    view.resize(720, 400);
    view.exec();
}
