#include "errors_panel.h"

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

ErrorsPanel::ErrorsPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    buildHeader(layout, "错误报告", "报错必须记录、可认领解决；解决说明只追加不覆盖");

    auto* toolbar = new QHBoxLayout;
    statusCombo_ = new QComboBox(this);
    statusCombo_->addItems({"全部", "open", "investigating", "resolved"});
    severityCombo_ = new QComboBox(this);
    severityCombo_->addItems({"全部", "info", "warning", "error", "critical"});
    resolveBtn_ = new QPushButton("登记解决", this);
    toolbar->addWidget(new QLabel("状态:", this));
    toolbar->addWidget(statusCombo_);
    toolbar->addWidget(new QLabel("严重度:", this));
    toolbar->addWidget(severityCombo_);
    toolbar->addStretch(1);
    toolbar->addWidget(resolveBtn_);
    layout->addLayout(toolbar);
    connect(statusCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(severityCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(resolveBtn_, &QPushButton::clicked, this, &ErrorsPanel::onResolve);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter_ = splitter;
    table_ = new QTableWidget(0, 5, splitter);
    table_->setHorizontalHeaderLabels({"严重度", "标题", "上报者", "状态", "时间"});
    table_->horizontalHeader()->setStretchLastSection(true);
    polishTable(table_);
    attachTableContextMenu(table_);
    splitter->addWidget(table_);

    auto* right = new QWidget(splitter);
    auto* rl = new QVBoxLayout(right);
    infoLabel_ = new QLabel(right);
    rl->addWidget(infoLabel_);
    detail_ = new QTextBrowser(right);
    rl->addWidget(detail_, 1);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);

    // 空状态引导语
    emptyLabel_ = new QLabel("暂无错误，一切正常 ✓", this);
    emptyLabel_->setStyleSheet("color:#22c55e; font-size:14px;");
    emptyLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(emptyLabel_);

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= static_cast<int>(errors_.size())) return;
        const auto& e = errors_[static_cast<size_t>(row)];
        infoLabel_->setText(QString("<b>%1</b> · 上报者: %2 · 来源: %3 · 状态: %4")
                                .arg(QString::fromStdString(e.title).toHtmlEscaped())
                                .arg(QString::fromStdString(e.reporter))
                                .arg(QString::fromStdString(e.source))
                                .arg(QString::fromStdString(e.status)));
        QString text = QString::fromStdString(e.detail);
        if (!e.stack_trace.empty())
            text += "\n\n---- 堆栈 ----\n" + QString::fromStdString(e.stack_trace);
        if (!e.resolution_notes.empty())
            text += "\n\n---- 解决记录（追加） ----\n" + QString::fromStdString(e.resolution_notes);
        detail_->setPlainText(text);
    });
}

void ErrorsPanel::refresh() {
    std::string status = statusCombo_->currentIndex() > 0
                             ? statusCombo_->currentText().toStdString()
                             : "";
    std::string severity = severityCombo_->currentIndex() > 0
                               ? severityCombo_->currentText().toStdString()
                               : "";
    std::string err;
    platform_.errorList(status, severity, 200, errors_, err);

    table_->setRowCount(static_cast<int>(errors_.size()));
    for (size_t i = 0; i < errors_.size(); ++i) {
        const auto& e = errors_[i];
        setRow(table_, static_cast<int>(i),
               {QString::fromStdString(e.severity), QString::fromStdString(e.title),
                QString::fromStdString(e.reporter), QString::fromStdString(e.status),
                QString::fromStdString(e.created_at)});
        // 严重度着色：critical 红 / error 橙 / warning 黄 / info 灰
        QString sev = QString::fromStdString(e.severity);
        QColor c = sev == "critical" ? ui::DANGER
                                     : (sev == "error" ? ui::WARN
                                                       : (sev == "warning" ? ui::NOTE : ui::MUTED));
        if (auto* item = table_->item(static_cast<int>(i), 0)) {
            item->setForeground(c);
            QFont f = item->font();
            f.setBold(sev == "critical" || sev == "error");
            item->setFont(f);
        }
    }
    emptyLabel_->setVisible(errors_.empty());
    splitter_->setVisible(!errors_.empty());
}

void ErrorsPanel::onResolve() {
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(errors_.size())) {
        QMessageBox::information(this, "解决", "请先选择一条错误");
        return;
    }
    const auto& e = errors_[static_cast<size_t>(row)];
    QDialog dlg(this);
    dlg.setWindowTitle(QString("登记解决: %1").arg(QString::fromStdString(e.title)));
    auto* l = new QVBoxLayout(&dlg);
    auto* notes = new QPlainTextEdit(&dlg);
    notes->setPlaceholderText("解决说明（追加到错误记录，不覆盖原内容）");
    l->addWidget(notes);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    l->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    zp::ErrorReport out;
    std::string err;
    if (!platform_.errorResolve("user", e.uuid, notes->toPlainText().toStdString(), out, err)) {
        ui::Toast::show(this, QString("解决失败: %1").arg(QString::fromStdString(err)), false);
        return;
    }
    ui::Toast::show(this, "已登记解决 ✓");
    refresh();
}
