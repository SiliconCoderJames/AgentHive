#pragma once
// 所有面板的公共基类：持有 Platform 引用，提供统一 refresh() 接口与页头构造。
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

// 经 zplatform_core 的 PUBLIC include 路径（src/）解析
#include "core/platform.h"

class PanelBase : public QWidget {
public:
    explicit PanelBase(zp::Platform& platform, QWidget* parent = nullptr)
        : QWidget(parent), platform_(platform) {}
    virtual void refresh() = 0;

protected:
    // 页头：面板标题 + 一句话说明，统一各面板的视觉节奏
    void buildHeader(QVBoxLayout* layout, const QString& title, const QString& subtitle) {
        auto* head = new QLabel(title, this);
        head->setStyleSheet("font-size:17px; font-weight:700; color:#e5e5e5;");
        auto* desc = new QLabel(subtitle, this);
        desc->setStyleSheet("font-size:11px; color:#9ca3af; margin-top:1px;");
        layout->addWidget(head);
        layout->addWidget(desc);
        layout->addSpacing(6);
    }

    zp::Platform& platform_;
};
