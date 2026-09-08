#pragma once
// 所有面板的公共基类：持有 Platform 引用，提供统一 refresh() 接口与页头构造。
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include "../i18n.h"
// 经 zplatform_core 的 PUBLIC include 路径（src/）解析
#include "core/platform.h"

class PanelBase : public QWidget {
public:
    explicit PanelBase(zp::Platform& platform, QWidget* parent = nullptr)
        : QWidget(parent), platform_(platform) {}
    virtual void refresh() = 0;
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
        headerTitle_->setStyleSheet("font-size:17px; font-weight:700; color:#e5e5e5;");
        headerSub_ = new QLabel(i18n::trs(zhSub, enSub), this);
        headerSub_->setStyleSheet("font-size:11px; color:#9ca3af; margin-top:1px;");
        layout->addWidget(headerTitle_);
        layout->addWidget(headerSub_);
        layout->addSpacing(6);
    }

    zp::Platform& platform_;

private:
    QLabel* headerTitle_ = nullptr;
    QLabel* headerSub_ = nullptr;
    QString zhTitle_, enTitle_, zhSub_, enSub_;
};
