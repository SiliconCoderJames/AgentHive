#pragma once
#include <QListWidget>
#include <QMainWindow>
#include <QLabel>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <functional>
#include <vector>

#include "core/platform.h"
#include "i18n.h"
#include "panels/panel_base.h"

class QFrame;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(zp::Platform& platform, QWidget* parent = nullptr);

private slots:
    void onNavChanged(int row);
    void onRefresh();

private:
    void buildNav();
    void buildStatusBar();
    void updateStatusBar();
    void applyLanguage();
    void rebuildPanels();
    void applyTheme();   // 主题/字号切换：重生成 QSS + 重涂铬层 + 重建面板
    void applyChrome();  // 侧边栏铬层样式（随主题重涂）

    zp::Platform& platform_;
    QListWidget* nav_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    std::vector<PanelBase*> panels_;
    std::vector<std::function<PanelBase*(zp::Platform&, QWidget*)>> panelFactories_;
    QWidget* side_ = nullptr;
    QFrame* brandLine_ = nullptr;
    QLabel* logo_ = nullptr;
    QLabel* statusServer_ = nullptr;
    QLabel* statusUsage_ = nullptr;
    QLabel* spin_ = nullptr;
    QToolButton* langBtn_ = nullptr;
    QToolButton* themeBtn_ = nullptr;
    QLabel* ver_ = nullptr;
    QLabel* tagline_ = nullptr;
    int spinPhase_ = 0;
    QTimer* timer_ = nullptr;
    int openErrors_ = 0;  // 未解决错误数，用于导航徽标
};
