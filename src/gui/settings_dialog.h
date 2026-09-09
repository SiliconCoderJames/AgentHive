#pragma once
// 设置对话框：外观（主题/字号）· 更新 · Agent API 接口 · 用量统计（日趋势+模型用量）。
// 主题/字号切换即时生效；对话框注册 theme 监听器，随主题刷新自身样式与图表。
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <cstddef>
#include <utility>
#include <vector>

#include "core/platform.h"
#include "widgets.h"

class QNetworkAccessManager;
class QTabWidget;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(zp::Platform& platform, QWidget* parent = nullptr);
    ~SettingsDialog() override;

private slots:
    void refreshUsage();
    void checkUpdate();

private:
    void buildAppearanceTab(QTabWidget* tabs);
    void buildUpdateTab(QTabWidget* tabs);
    void buildApiTab(QTabWidget* tabs);
    void buildUsageTab(QTabWidget* tabs);
    // 主题/字号变化后重涂对话框内以 th() 取色的控件与图表
    void applyChrome();
    QLabel* thLabel(const QString& tmpl, QWidget* parent);

    zp::Platform& platform_;
    QComboBox* themeBox_ = nullptr;
    QComboBox* fontBox_ = nullptr;
    QLabel* latest_ = nullptr;            // 更新页状态行
    QLabel* usageHead_ = nullptr;         // 用量页汇总行
    ui::VBarChart* dailyChart_ = nullptr;
    ui::HBarChart* modelChart_ = nullptr;
    QPointer<QNetworkAccessManager> net_;
    QTimer* usageTimer_ = nullptr;
    std::size_t themeListenerId_ = 0;
    // th() 取色控件登记表：切换主题时统一重涂（含图表刷新的去抖缓存）
    std::vector<std::pair<QLabel*, QString>> styledLabels_;
    QString lastHead_;
    QVector<QPair<QString, qint64>> lastDaily_, lastModel_;
};
