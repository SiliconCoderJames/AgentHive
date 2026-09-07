#pragma once
// 态势感知工作台自定义控件：环形进度、横向柱状图、Toast、Agent 卡片、
// 告警卡片、可折叠区块卡片。全部 QPainter / 原生 widget 实现，无 QML。
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidget>

#include "theme.h"

namespace ui {

// 卡片悬停高亮：进入边框变强调蓝（AgentCard / 通用 QFrame 卡片）
inline void hoverGlow(QFrame* frame, const char* objectName) {
    frame->setAttribute(Qt::WA_Hover);
    QString base = QString("QFrame#%1 { background:#2d2d2d; border:1px solid #3f3f46;"
                           " border-radius:8px; }").arg(objectName);
    QString hover = QString("QFrame#%1:hover { background:#313131; border:1px solid %2;"
                            " border-radius:8px; }").arg(objectName, ACCENT.name());
    frame->setStyleSheet(base + hover);
}

// ---- 环形进度：中间显示 已用/总额/百分比，临近预算橙→红，进度弧平滑动画 ----
class RingProgress : public QWidget {
public:
    RingProgress(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(170, 170);
    }
    void setValues(qint64 used, qint64 total, const QString& caption) {
        used_ = used;
        total_ = total > 0 ? total : 1;
        caption_ = caption;
        // 进度弧从当前角度平滑扫掠到目标，避免 3s 刷新时生硬跳变
        double target = qMin(1.0, double(used_) / double(total_));
        if (anim_) anim_->stop();  // QPointer：动画自删后自动置空，安全
        auto* anim = new QVariantAnimation(this);
        anim_ = anim;
        anim->setDuration(450);
        anim->setStartValue(animRatio_);
        anim->setEndValue(target);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            animRatio_ = v.toDouble();
            update();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        int side = qMin(width(), height());
        QRectF rect((width() - side) / 2 + 10, (height() - side) / 2 + 10,
                    side - 20, side - 20);
        double ratio = animRatio_;
        // 背景环
        QPen pen(QColor("#3f3f46"), 12, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.drawArc(rect, 45 * 16, -270 * 16);
        // 进度环（占比驱动颜色：蓝→橙→红渐变过渡）
        QColor c = usageColor(ratio);
        if (ratio > 0.001) {
            QPen prog(c, 12, Qt::SolidLine, Qt::RoundCap);
            p.setPen(prog);
            p.drawArc(rect, 45 * 16, int(-270 * 16 * ratio));
        }
        // 中心文字
        p.setPen(QPen(TEXT));
        QFont f = p.font();
        f.setPixelSize(side / 6);
        f.setBold(true);
        p.setFont(f);
        QRectF center = rect.adjusted(20, 20, -20, -20);
        p.drawText(center.adjusted(0, -14, 0, -14), Qt::AlignCenter,
                   QString("%1%").arg(ratio * 100, 0, 'f', 1));
        f.setPixelSize(side / 14);
        p.setFont(f);
        p.setPen(QPen(MUTED));
        auto fmt = [](qint64 v) {
            QString s = QString::number(v);
            for (int i = s.size() - 3; i > 0; i -= 3) s.insert(i, ',');
            return s;
        };
        p.drawText(center.adjusted(0, 16, 0, 16), Qt::AlignCenter,
                   QString("%1 / %2").arg(fmt(used_), fmt(total_)));
        if (!caption_.isEmpty()) {
            p.drawText(center.adjusted(0, 40, 0, 40), Qt::AlignCenter, caption_);
        }
    }

private:
    qint64 used_ = 0, total_ = 1;
    QString caption_;
    double animRatio_ = 0.0;      // 动画当前扫掠比例（与目标值的差由 QVariantAnimation 收敛）
    QPointer<QVariantAnimation> anim_;  // DeleteWhenStopped 会自删，用 QPointer 防悬空
};

// ---- 横向柱状图：各 Agent 用量对比，柱体平滑扫掠入场 ----
class HBarChart : public QWidget {
public:
    explicit HBarChart(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(120);
    }
    void setEntries(const QVector<QPair<QString, qint64>>& entries) {
        entries_ = entries;
        if (anim_) anim_->stop();
        auto* anim = new QVariantAnimation(this);
        anim_ = anim;
        anim->setDuration(450);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            sweep_ = v.toDouble();
            update();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (entries_.isEmpty()) {
            p.setPen(QPen(MUTED));
            p.drawText(rect(), Qt::AlignCenter, "暂无用量数据");
            return;
        }
        qint64 maxV = 1;
        for (const auto& e : entries_) maxV = qMax(maxV, e.second);
        int rowH = height() / entries_.size();
        int labelW = qMin(110, width() / 4);
        int valueW = 110;
        int barX = labelW + 8;
        int barW = width() - barX - valueW - 8;
        QFont f = p.font();
        f.setPixelSize(11);
        p.setFont(f);
        auto fmt = [](qint64 v) {
            QString s = QString::number(v);
            for (int i = s.size() - 3; i > 0; i -= 3) s.insert(i, ',');
            return s;
        };
        for (int i = 0; i < entries_.size(); ++i) {
            int y = i * rowH;
            p.setPen(QPen(TEXT));
            p.drawText(QRect(0, y, labelW - 4, rowH), Qt::AlignVCenter | Qt::AlignRight,
                       entries_[i].first);
            // 底槽
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#3f3f46"));
            p.drawRoundedRect(QRect(barX, y + rowH / 2 - 5, barW, 10), 5, 5);
            // 柱体：最大值高亮蓝，其余随占比变暗；宽度乘以入场扫掠进度
            double ratio = double(entries_[i].second) / double(maxV) * sweep_;
            int w = int(double(barW) * ratio);
            QColor bar = ACCENT;
            if (entries_[i].second != maxV) {
                bar = ACCENT.darker(100 + int((1.0 - ratio) * 90));
                bar.setAlpha(210);
            }
            p.setBrush(bar);
            p.drawRoundedRect(QRect(barX, y + rowH / 2 - 5, qMax(w, 4), 10), 5, 5);
            // 数值 + 占比（等宽）
            QFont mf = p.font();
            mf.setFamily(mono());
            p.setFont(mf);
            p.setPen(QPen(MUTED));
            p.drawText(QRect(width() - valueW, y, valueW, rowH), Qt::AlignVCenter,
                       QString("%1 · %2%").arg(fmt(entries_[i].second))
                           .arg(ratio * 100, 0, 'f', 0));
            p.setFont(f);
        }
    }

private:
    QVector<QPair<QString, qint64>> entries_;
    double sweep_ = 1.0;
    QPointer<QVariantAnimation> anim_;
};

// ---- Toast：右下角气泡提示（成功绿 / 失败红），1.8s 自动消失 ----
class Toast : public QLabel {
public:
    static void show(QWidget* parent, const QString& msg, bool success = true) {
        auto* t = new Toast(parent, msg, success);
        t->popup();
    }

private:
    Toast(QWidget* parent, const QString& msg, bool success) : QLabel(msg, parent) {
        setObjectName(success ? "toastOk" : "toastErr");
        setAttribute(Qt::WA_DeleteOnClose);
        setStyleSheet(success
            ? "QLabel#toastOk { background:#064e3b; color:#a7f3d0; border:1px solid #22c55e;"
              " border-radius:8px; padding:10px 18px; font-size:12px; }"
            : "QLabel#toastErr { background:#7f1d1d; color:#fecaca; border:1px solid #ef4444;"
              " border-radius:8px; padding:10px 18px; font-size:12px; }");
        adjustSize();
    }
    void popup() {
        QWidget* top = parentWidget();
        while (top && !top->isWindow()) top = top->parentWidget();
        if (!top) { deleteLater(); return; }
        // 提升为顶层独立气泡，置于右下角，淡出消失
        setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        QPoint pos = top->pos() + QPoint(top->width() - width() - 24,
                                         top->height() - height() - 46);
        move(pos);
        QWidget::show();  // 显式调用基类，避免被静态 show(QString,bool) 遮蔽
        auto* fx = new QGraphicsOpacityEffect(this);
        fx->setOpacity(1.0);
        setGraphicsEffect(fx);
        auto* anim = new QPropertyAnimation(fx, "opacity", this);
        anim->setDuration(500);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        anim->setEasingCurve(QEasingCurve::InQuad);
        QTimer::singleShot(1500, this, [anim] { anim->start(QAbstractAnimation::DeleteWhenStopped); });
        QTimer::singleShot(2050, this, &QObject::deleteLater);
    }
};

// ---- Agent 状态卡片：名称 + 指示灯 + 任务摘要 + 最后活跃 ----
class AgentCard : public QFrame {
public:
    explicit AgentCard(QWidget* parent = nullptr) : QFrame(parent) {
        setObjectName("agentCard");
        hoverGlow(this, "agentCard");
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(14, 12, 14, 12);
        lay->setSpacing(6);
        auto* head = new QHBoxLayout();
        name_ = new QLabel(this);
        name_->setStyleSheet("font-size:13px; font-weight:700; background:transparent;");
        dot_ = new QLabel(this);
        dot_->setFixedWidth(14);
        dot_->setAlignment(Qt::AlignCenter);
        status_ = new QLabel(this);
        status_->setStyleSheet(
            "font-size:10px; padding:1px 8px; border-radius:8px;"
            "color:#a7f3d0; background:#064e3b;");
        role_ = new QLabel(this);
        role_->setStyleSheet("color:#9ca3af; font-size:11px; background:transparent;");
        head->addWidget(dot_);
        head->addWidget(name_);
        head->addWidget(status_);
        head->addStretch(1);
        head->addWidget(role_);
        lay->addLayout(head);
        task_ = new QLabel(this);
        task_->setStyleSheet("color:#e5e5e5; font-size:11px; background:transparent;");
        task_->setWordWrap(true);
        lay->addWidget(task_);
        seen_ = new QLabel(this);
        seen_->setStyleSheet(
            "color:#9ca3af; font-size:10px; font-family:Consolas,monospace; background:transparent;");
        lay->addWidget(seen_);
        // 在线呼吸灯：点亮的绿点每 900ms 明暗交替，离线则恒灰
        pulse_ = new QTimer(this);
        pulse_->setInterval(900);
        connect(pulse_, &QTimer::timeout, this, [this] {
            pulseOn_ = !pulseOn_;
            updateDot();
        });
        pulse_->start();
    }
    void setAgent(const QString& name, const QString& status, const QString& role,
                  const QString& task, const QString& lastSeen) {
        name_->setText(name);
        online_ = status == "online";
        updateDot();
        status_->setText(online_ ? "在线" : "离线");
        status_->setStyleSheet(online_
            ? "font-size:10px; padding:1px 8px; border-radius:8px;"
              "color:#a7f3d0; background:#064e3b;"
            : "font-size:10px; padding:1px 8px; border-radius:8px;"
              "color:#a1a1aa; background:#27272a;");
        role_->setText(role);
        task_->setText(task.isEmpty() ? "（无当前任务）" : task);
        seen_->setText(lastSeen.isEmpty() ? "从未活跃" : "活跃于 " + lastSeen);
    }

private:
    QLabel* name_ = nullptr;
    QLabel* dot_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* role_ = nullptr;
    QLabel* task_ = nullptr;
    QLabel* seen_ = nullptr;
    QTimer* pulse_ = nullptr;
    bool online_ = false;
    bool pulseOn_ = true;
    void updateDot() {
        dot_->setText(online_ ? QString("<span style='color:%1;'>●</span>")
                                    .arg(pulseOn_ ? "#22c55e" : "#15803d")
                              : "<span style='color:#71717a;'>●</span>");
    }
};

// ---- 告警卡片：红=阻断 / 橙=警告 / 黄=注意 ----
class AlertCard : public QFrame {
public:
    AlertCard(const QString& severity, const QString& text, QWidget* parent = nullptr)
        : QFrame(parent) {
        QColor c = severity == "critical" ? DANGER : (severity == "warn" ? WARN : NOTE);
        setStyleSheet(QString("QFrame { background:#2d2d2d; border-left:4px solid %1;"
                              " border-radius:6px; padding:2px; }").arg(c.name()));
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 8, 12, 8);
        auto* label = new QLabel(text, this);
        label->setWordWrap(true);
        label->setStyleSheet(QString("color:%1; font-size:12px;").arg(c.name()));
        lay->addWidget(label);
    }
};

// ---- 可折叠区块卡片（用户记忆分组等）----
class SectionCard : public QWidget {
public:
    SectionCard(const QString& title, QWidget* content, QWidget* parent = nullptr)
        : QWidget(parent), content_(content) {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        auto* toggle = new QToolButton(this);
        toggle->setText("▾  " + title);
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setStyleSheet(
            "QToolButton { background:#2d2d2d; border:1px solid #3f3f46;"
            " border-radius:8px; padding:10px 14px; font-size:13px; font-weight:600;"
            " text-align:left; }"
            "QToolButton:hover { border-color:#0ea5e9; }");
        content_->setStyleSheet(
            "QWidget { background:#262626; border:1px solid #3f3f46;"
            " border-top:none; border-radius:0 0 8px 8px; }");
        lay->addWidget(toggle);
        lay->addWidget(content_);
        connect(toggle, &QToolButton::toggled, this, [this, toggle](bool on) {
            content_->setVisible(on);
            toggle->setText(on ? "▾  " + toggle->text().mid(3)
                               : "▸  " + toggle->text().mid(3));
        });
    }

private:
    QWidget* content_;
};

}  // namespace ui
