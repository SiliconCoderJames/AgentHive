#include "logs_panel.h"

#include <QBrush>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include "../gui_util.h"
#include "../i18n.h"
#include "../widgets.h"

LogsPanel::LogsPanel(zp::Platform& platform, QWidget* parent)
    : PanelBase(platform, parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    buildHeader(layout, "操作日志", "Audit Log",
                "谁、什么时候、做了什么——全部写操作可追溯，按天分组浏览",
                "Every write, by whom and when - grouped by day");

    auto* toolbar = new QHBoxLayout;
    agentCombo_ = new QComboBox(this);
    agentCombo_->addItem(i18n::trs("全部身份", "All identities"));
    sinceEdit_ = new QDateEdit(this);
    sinceEdit_->setDisplayFormat("yyyy-MM-dd");
    sinceEdit_->setCalendarPopup(true);
    auto* refreshBtn = new QPushButton(i18n::trs("筛选", "Apply"), this);
    refreshBtn->setObjectName("primary");
    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet(ui::th("color:@muted@; font-size:11px;"));
    toolbar->addWidget(new QLabel(i18n::trs("身份:", "Actor:"), this));
    toolbar->addWidget(agentCombo_);
    toolbar->addWidget(new QLabel(i18n::trs("起始日期:", "Since:"), this));
    toolbar->addWidget(sinceEdit_);
    toolbar->addWidget(refreshBtn);
    toolbar->addStretch(1);
    toolbar->addWidget(countLabel_);
    layout->addLayout(toolbar);
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
    connect(agentCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });

    // 时间线：顶层 = 日期，子项 = 该日操作
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({i18n::trs("时间", "Time"), i18n::trs("身份", "Actor"), i18n::trs("动作", "Action"),i18n::trs("对象", "Target"), i18n::trs("详情", "Detail")});
    tree_->header()->setStretchLastSection(true);
    tree_->setAlternatingRowColors(true);
    layout->addWidget(tree_, 1);
}

void LogsPanel::refresh() {
    // 身份下拉（含全部身份 + user + 各 Agent）
    std::vector<zp::AgentInfo> agents;
    std::string err;
    platform_.listAgents(agents, err);
    QString cur = agentCombo_->currentText();
    agentCombo_->blockSignals(true);
    agentCombo_->clear();
    agentCombo_->addItem(i18n::trs("全部身份", "All identities"));
    agentCombo_->addItem("user");
    for (const auto& a : agents) agentCombo_->addItem(QString::fromStdString(a.name));
    agentCombo_->setCurrentText(cur);
    agentCombo_->blockSignals(false);

    std::string actor;
    if (agentCombo_->currentIndex() > 0) actor = agentCombo_->currentText().toStdString();
    std::string since = sinceEdit_->date().toString("yyyy-MM-dd").toStdString() + "T00:00:00Z";
    platform_.auditList(actor, "", since, 2000, records_, err);

    // 时间线分组
    tree_->clear();
    QTreeWidgetItem* dayItem = nullptr;
    QString curDay;
    for (const auto& r : records_) {
        QString ts = QString::fromStdString(r.created_at);
        QString day = ts.left(10);
        if (day != curDay) {
            curDay = day;
            dayItem = new QTreeWidgetItem(tree_, {day, "", "", "", ""});
            tree_->setFirstColumnSpanned(tree_->indexOfTopLevelItem(dayItem),
                                         QModelIndex(), true);
            QFont f = dayItem->font(0);
            f.setBold(true);
            dayItem->setFont(0, f);
            dayItem->setForeground(0, QBrush(ui::accent()));
        }
        auto* row = new QTreeWidgetItem(dayItem);
        row->setText(0, ts.mid(11, 8));
        row->setText(1, QString::fromStdString(r.actor));
        row->setText(2, QString::fromStdString(r.action));
        row->setText(3, QString::fromStdString(r.target));
        row->setText(4, QString::fromStdString(r.detail));
        for (int c = 0; c < 5; ++c) row->setFlags(row->flags() & ~Qt::ItemIsEditable);
    }
    tree_->expandToDepth(0);
    countLabel_->setText(i18n::trs("共 %1 条", "%1 records").arg(formatNum(static_cast<qint64>(records_.size()))));
}
