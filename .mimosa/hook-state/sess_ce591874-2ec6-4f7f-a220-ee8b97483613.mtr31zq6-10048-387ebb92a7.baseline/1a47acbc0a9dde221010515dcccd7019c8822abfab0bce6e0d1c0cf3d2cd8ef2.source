#include "logs_panel.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include "../gui_util.h"

LogsPanel::LogsPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);

    auto* toolbar = new QHBoxLayout;
    actorEdit_ = new QLineEdit(this);
    actorEdit_->setPlaceholderText("按身份筛选（Agent 名 / user）");
    actionEdit_ = new QLineEdit(this);
    actionEdit_->setPlaceholderText("按动作筛选（如 knowledge.create）");
    limitSpin_ = new QSpinBox(this);
    limitSpin_->setRange(10, 2000);
    limitSpin_->setValue(500);
    auto* refreshBtn = new QPushButton("筛选", this);
    toolbar->addWidget(actorEdit_, 1);
    toolbar->addWidget(actionEdit_, 1);
    toolbar->addWidget(new QLabel("条数:", this));
    toolbar->addWidget(limitSpin_);
    toolbar->addWidget(refreshBtn);
    layout->addLayout(toolbar);
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
    connect(actorEdit_, &QLineEdit::returnPressed, this, [this] { refresh(); });
    connect(actionEdit_, &QLineEdit::returnPressed, this, [this] { refresh(); });

    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({"时间", "身份", "动作", "对象", "详情"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);
}

void LogsPanel::refresh() {
    std::string err;
    platform_.auditList(actorEdit_->text().trimmed().toStdString(),
                        actionEdit_->text().trimmed().toStdString(), "",
                        limitSpin_->value(), records_, err);

    table_->setRowCount(static_cast<int>(records_.size()));
    for (size_t i = 0; i < records_.size(); ++i) {
        const auto& r = records_[i];
        setRow(table_, static_cast<int>(i),
               {QString::fromStdString(r.created_at), QString::fromStdString(r.actor),
                QString::fromStdString(r.action), QString::fromStdString(r.target),
                QString::fromStdString(r.detail)});
    }
}
