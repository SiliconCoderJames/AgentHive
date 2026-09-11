#pragma once
// 设置对话框：左侧窄导航 + 右侧内容堆叠（对齐主流 AI 桌面应用范式）。
// 分区：外观（主题色卡/字号）· 数据与备份 · 通知偏好 · Agent 管理 · Agent API · 更新。
// 主题/字号切换即时生效；对话框注册 theme 监听器，随主题刷新自身样式。
#include <QDialog>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <cstddef>
#include <utility>
#include <vector>

#include "core/platform.h"
#include "widgets.h"

class QCheckBox;
class QComboBox;
class QNetworkAccessManager;
class QListWidget;
class QStackedWidget;
class QTableWidget;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(ah::Platform& platform, QWidget* parent = nullptr);
    ~SettingsDialog() override;

protected:
    void showEvent(QShowEvent*) override;  // 每次打开刷新备份列表与 Agent 列表

private:
    QWidget* buildAppearancePage();
    QWidget* buildBackupPage();
    QWidget* buildNotifyPage();
    QWidget* buildAgentsPage();
    QWidget* buildApiPage();
    QWidget* buildUpdatePage();
    QWidget* buildAboutPage();
    void refreshBackupList();
    void refreshAgents();
    void applyChrome();  // 主题变化后重涂导航与 th() 取色控件
    QLabel* thLabel(const QString& tmpl, QWidget* parent);

    ah::Platform& platform_;
    QListWidget* nav_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    std::vector<ui::ThemeSwatch*> swatches_;
    QComboBox* fontBox_ = nullptr;
    QTableWidget* backupTable_ = nullptr;
    QComboBox* refreshBox_ = nullptr;
    QCheckBox* errorToastBox_ = nullptr;
    QTableWidget* agentsTable_ = nullptr;
    QLabel* latest_ = nullptr;  // 更新页状态行
    QPointer<QNetworkAccessManager> net_;
    std::size_t themeListenerId_ = 0;
    // th() 取色控件登记表：切换主题时统一重涂
    std::vector<std::pair<QLabel*, QString>> styledLabels_;
};
