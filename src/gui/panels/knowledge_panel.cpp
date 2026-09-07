#include "knowledge_panel.h"

#include <algorithm>
#include <map>

#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>

#include "../gui_util.h"
#include "../widgets.h"
#include "core/util.h"

KnowledgePanel::KnowledgePanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    buildHeader(layout, "知识库", "经验 / 方案 / 踩坑统一沉淀，关键词与语义双模式检索，版本只追加不覆盖");

    // 工具栏
    auto* toolbar = new QHBoxLayout;
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("搜索知识库（关键词或自然语言）…");
    semanticCheck_ = new QCheckBox("语义搜索", this);
    tagEdit_ = new QLineEdit(this);
    tagEdit_->setPlaceholderText("按标签过滤");
    auto* searchBtn = new QPushButton("搜索", this);
    auto* newBtn = new QPushButton("＋ 新建条目", this);
    toolbar->addWidget(searchEdit_, 1);
    toolbar->addWidget(semanticCheck_);
    toolbar->addWidget(tagEdit_);
    toolbar->addWidget(searchBtn);
    toolbar->addWidget(newBtn);
    layout->addLayout(toolbar);
    connect(searchBtn, &QPushButton::clicked, this, &KnowledgePanel::onSearch);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &KnowledgePanel::onSearch);
    connect(newBtn, &QPushButton::clicked, this, &KnowledgePanel::onNewEntry);

    // 统计摘要栏：总条目数 / 今日新增 / 热门标签
    statsLabel_ = new QLabel(this);
    statsLabel_->setObjectName("muted");
    layout->addWidget(statsLabel_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    table_ = new QTableWidget(0, 5, splitter);
    table_->setHorizontalHeaderLabels({"标题", "作者", "标签", "版本", "时间"});
    table_->horizontalHeader()->setStretchLastSection(true);
    polishTable(table_);
    splitter->addWidget(table_);

    auto* right = new QWidget(splitter);
    auto* rl = new QVBoxLayout(right);
    metaLabel_ = new QLabel(right);
    rl->addWidget(metaLabel_);
    detail_ = new QTextBrowser(right);
    rl->addWidget(detail_, 1);
    auto* vrow = new QHBoxLayout;
    versionCombo_ = new QComboBox(right);
    addVersionBtn_ = new QPushButton("追加新版本", right);
    vrow->addWidget(versionCombo_, 1);
    vrow->addWidget(addVersionBtn_);
    rl->addLayout(vrow);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) { onSelectEntry(row); });
    connect(versionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &KnowledgePanel::onVersionChanged);
    connect(addVersionBtn_, &QPushButton::clicked, this, &KnowledgePanel::onAddVersion);
    attachTableContextMenu(table_);
}

void KnowledgePanel::refresh() {
    if (table_->rowCount() == 0) onSearch();
}

void KnowledgePanel::onSearch() {
    std::string err;
    hits_.clear();
    std::string tag = tagEdit_->text().trimmed().toStdString();
    std::string query = searchEdit_->text().trimmed().toStdString();
    if (query.empty()) {
        std::vector<zp::KnowledgeEntry> entries;
        if (platform_.knowledgeList(200, tag, entries, err)) {
            for (auto& e : entries) hits_.push_back({std::move(e), 0.0});
        }
    } else {
        zp::SearchMode mode =
            semanticCheck_->isChecked() ? zp::SearchMode::Semantic : zp::SearchMode::Keyword;
        platform_.knowledgeSearch(query, mode, 50, tag, hits_, err);
    }

    table_->setRowCount(static_cast<int>(hits_.size()));
    for (size_t i = 0; i < hits_.size(); ++i) {
        const auto& e = hits_[i].entry;
        QString tags;
        for (const auto& t : e.tags) tags += QString::fromStdString(t) + " ";
        setRow(table_, static_cast<int>(i),
               {QString::fromStdString(e.title), QString::fromStdString(e.author), tags.trimmed(),
                QString::number(e.version), QString::fromStdString(e.created_at)});
    }

    // 统计摘要：总数 / 今日新增 / 热门标签
    QString today = QString::fromStdString(zp::nowIso()).left(10);
    int todayCount = 0;
    std::map<QString, int> tagFreq;
    for (const auto& h : hits_) {
        if (QString::fromStdString(h.entry.created_at).startsWith(today)) ++todayCount;
        for (const auto& t : h.entry.tags) ++tagFreq[QString::fromStdString(t)];
    }
    QVector<QPair<QString, int>> top(tagFreq.begin(), tagFreq.end());
    std::sort(top.begin(), top.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    QString tagStr;
    for (int i = 0; i < qMin(5, static_cast<int>(top.size())); ++i)
        tagStr += QString("%1(%2) ").arg(top[i].first).arg(top[i].second);
    statsLabel_->setText(QString("总条目 %1 · 今日新增 %2 · 热门标签: %3")
                             .arg(formatNum(static_cast<qint64>(hits_.size())))
                             .arg(todayCount)
                             .arg(tagStr.isEmpty() ? "—" : tagStr));

    versionCombo_->clear();
    if (!hits_.empty()) {
        table_->selectRow(0);
        onSelectEntry(0);
    } else {
        // 空状态提示：避免面板显得单调且无从下手
        detail_->setHtml(
            "<div style='color:#9ca3af; text-align:center; margin-top:36px;'>"
            "<div style='font-size:34px;'>📚</div>"
            "暂无知识条目<br><br>点击右上角「＋ 新建条目」沉淀第一条经验，"
            "或让任意已接入的 Agent 通过 <span style='font-family:Consolas;'>POST /api/knowledge</span> 写入"
            "</div>");
    }
}

void KnowledgePanel::onSelectEntry(int row) {
    if (row < 0 || row >= static_cast<int>(hits_.size())) return;
    const auto& hit = hits_[static_cast<size_t>(row)];
    const auto& e = hit.entry;
    currentUuid_ = e.uuid;
    detail_->setPlainText(QString::fromStdString(e.content));
    QString meta = QString("<b>%1</b> · 作者: %2 · 版本 v%3 · 分类: %4 · 时间: %5")
                       .arg(QString::fromStdString(e.title).toHtmlEscaped())
                       .arg(QString::fromStdString(e.author))
                       .arg(e.version)
                       .arg(QString::fromStdString(e.category))
                       .arg(QString::fromStdString(e.created_at));
    if (semanticCheck_->isChecked())
        meta += QString(" · 距离: %1").arg(hit.score, 0, 'f', 4);
    metaLabel_->setText(meta);

    // 历史版本
    versionCombo_->blockSignals(true);
    versionCombo_->clear();
    versions_.clear();
    std::string err;
    if (platform_.knowledgeVersions(e.uuid, versions_, err)) {
        for (const auto& v : versions_)
            versionCombo_->addItem(QString("v%1 — %2 (%3)")
                                       .arg(v.version)
                                       .arg(QString::fromStdString(v.author))
                                       .arg(QString::fromStdString(v.created_at)));
    }
    versionCombo_->setCurrentIndex(0);
    versionCombo_->blockSignals(false);
}

void KnowledgePanel::onVersionChanged(int idx) {
    if (idx < 0 || idx >= static_cast<int>(versions_.size())) return;
    const auto& v = versions_[static_cast<size_t>(idx)];
    currentUuid_ = v.uuid;
    detail_->setPlainText(QString::fromStdString(v.content));
    metaLabel_->setText(QString("<b>%1</b> · 作者: %2 · 版本 v%3 · 时间: %4")
                            .arg(QString::fromStdString(v.title).toHtmlEscaped())
                            .arg(QString::fromStdString(v.author))
                            .arg(v.version)
                            .arg(QString::fromStdString(v.created_at)));
}

void KnowledgePanel::onNewEntry() {
    QDialog dlg(this);
    dlg.setWindowTitle("新建知识条目");
    auto* form = new QFormLayout(&dlg);
    auto* title = new QLineEdit(&dlg);
    auto* content = new QPlainTextEdit(&dlg);
    auto* tags = new QLineEdit(&dlg);
    tags->setPlaceholderText("逗号分隔，如 qt,cmake");
    auto* category = new QLineEdit(&dlg);
    form->addRow("标题", title);
    form->addRow("内容", content);
    form->addRow("标签", tags);
    form->addRow("分类", category);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    std::vector<std::string> tagList;
    for (const auto& t : tags->text().split(',', Qt::SkipEmptyParts))
        tagList.push_back(t.trimmed().toStdString());
    std::string err;
    zp::KnowledgeEntry out;
    if (!platform_.knowledgeCreate("user", title->text().trimmed().toStdString(),
                                   content->toPlainText().toStdString(), tagList,
                                   category->text().trimmed().toStdString(), {}, "", out, err)) {
        ui::Toast::show(this, QString("创建失败: %1").arg(QString::fromStdString(err)), false);
        return;
    }
    ui::Toast::show(this, "知识条目已创建 ✓");
    onSearch();
}

void KnowledgePanel::onAddVersion() {
    if (currentUuid_.empty()) return;
    QDialog dlg(this);
    dlg.setWindowTitle("追加新版本（旧版本保留，禁止覆盖）");
    auto* form = new QFormLayout(&dlg);
    auto* title = new QLineEdit(&dlg);
    auto* content = new QPlainTextEdit(&dlg);
    form->addRow("标题（留空沿用当前版本标题）", title);
    form->addRow("新内容", content);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    std::string err;
    zp::KnowledgeEntry out;
    if (!platform_.knowledgeAddVersion("user", currentUuid_, title->text().trimmed().toStdString(),
                                       content->toPlainText().toStdString(), {}, "", out, err)) {
        ui::Toast::show(this, QString("追加失败: %1").arg(QString::fromStdString(err)), false);
        return;
    }
    ui::Toast::show(this, QString("已追加 v%1（旧版本保留）✓").arg(out.version));
    onSearch();
}
