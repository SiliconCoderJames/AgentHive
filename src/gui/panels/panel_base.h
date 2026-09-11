#pragma once
// 所有面板的公共基类：持有 Platform 引用，提供统一 refresh() 接口与页头构造。
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include "../i18n.h"
#include "../theme.h"
// 经 zplatform_core 的 PUBLIC include 路径（src/）解析
#include "core/platform.h"

class PanelBase : public QWidget {
public:
    explicit PanelBase(zp::Platform& platform, QWidget* parent = nullptr)
        : QWidget(parent), platform_(platform) {}
    virtual void refresh() = 0;
    // Ctrl+F：把焦点交给本面板的即时过滤框；无过滤框的面板不用覆写
    virtual void focusFilter() {}
    // 语言切换时重译铬层文案；面板有自有文案时覆写并先调用基类
    virtual void retranslate() {
        if (headerTitle_) headerTitle_->setText(i18n::trs(zhTitle_, enTitle_));
        if (headerSub_) headerSub_->setText(i18n::trs(zhSub_, enSub_));
    }

protected:
    // 页头：面板标题 + 一句话说明，统一各面板的视觉节奏（双语，可重译）
    void buildHeader(QVBoxLayout* layout, const QString& zhTitle, const QString& enTitle,
                     const QString& zhSub, const QString& enSub) {
        zhTitle_ = zhTitle; enTitle_ = enTitle;
        zhSub_ = zhSub; enSub_ = enSub;
        headerTitle_ = new QLabel(i18n::trs(zhTitle, enTitle), this);
        headerTitle_->setStyleSheet(
            ui::th("font-size:20px; font-weight:700; color:@text@;"));
        // 竖向固定：否则面板高于内容时，多余空间会被标题/副标题吸收，
        // 标题与正文之间被撑出一大片空白（内容区反而没拿到空间）
        headerTitle_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        headerSub_ = new QLabel(i18n::trs(zhSub, enSub), this);
        headerSub_->setStyleSheet(ui::th("font-size:12px; color:@muted@; margin-top:2px;"));
        headerSub_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        // 标题蜜金→天蓝竖向渐变饰条：全面板统一的品牌签名
        auto* bar = new QFrame(this);
        bar->setFixedSize(4, 26);
        bar->setStyleSheet(
            ui::th("background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                   "stop:0 @brand@, stop:1 @accent@); border-radius:2px;"));
        auto* head = new QHBoxLayout;
        head->setSpacing(10);
        head->addWidget(bar);
        head->addWidget(headerTitle_);
        head->addStretch(1);
        layout->addLayout(head);
        layout->addWidget(headerSub_);
        layout->addSpacing(10);
    }

    zp::Platform& platform_;

private:
    QLabel* headerTitle_ = nullptr;
    QLabel* headerSub_ = nullptr;
    QString zhTitle_, enTitle_, zhSub_, enSub_;
};
