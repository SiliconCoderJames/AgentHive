#pragma once
#include <QListWidget>
#include <QMainWindow>
#include <QLabel>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <vector>

#include "core/platform.h"
#include "i18n.h"
#include "panels/panel_base.h"

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

    zp::Platform& platform_;
    QListWidget* nav_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    std::vector<PanelBase*> panels_;
    QLabel* statusServer_ = nullptr;
    QLabel* statusUsage_ = nullptr;
    QLabel* spin_ = nullptr;
    QToolButton* langBtn_ = nullptr;
    QLabel* tagline_ = nullptr;
    int spinPhase_ = 0;
    QTimer* timer_ = nullptr;
    int openErrors_ = 0;  // 未解决错误数，用于导航徽标
};
