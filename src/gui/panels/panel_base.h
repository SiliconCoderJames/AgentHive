#pragma once
// 所有面板的公共基类：持有 Platform 引用，提供统一 refresh() 接口。
#include <QWidget>

// 经 zplatform_core 的 PUBLIC include 路径（src/）解析
#include "core/platform.h"

class PanelBase : public QWidget {
public:
    explicit PanelBase(zp::Platform& platform, QWidget* parent = nullptr)
        : QWidget(parent), platform_(platform) {}
    virtual void refresh() = 0;

protected:
    zp::Platform& platform_;
};
