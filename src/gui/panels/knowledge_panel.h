#pragma once
// 知识库面板：关键词 / 语义搜索、条目浏览、新建条目、追加版本。
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QLabel>
#include <QComboBox>

#include <vector>

#include "panel_base.h"

class KnowledgePanel : public PanelBase {
    Q_OBJECT
public:
    explicit KnowledgePanel(zp::Platform& platform, QWidget* parent = nullptr);
    void refresh() override;

private slots:
    void onSearch();
    void onNewEntry();
    void onAddVersion();
    void onSelectEntry(int row);
    void onVersionChanged(int idx);

private:
    QLineEdit* searchEdit_ = nullptr;
    QCheckBox* semanticCheck_ = nullptr;
    QLineEdit* tagEdit_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextBrowser* detail_ = nullptr;
    QLabel* metaLabel_ = nullptr;
    QComboBox* versionCombo_ = nullptr;
    QPushButton* addVersionBtn_ = nullptr;
    std::vector<zp::KnowledgeHit> hits_;
    std::vector<zp::KnowledgeEntry> versions_;
    std::string currentUuid_;
};
