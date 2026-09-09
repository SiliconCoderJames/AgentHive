#pragma once
// 态势感知工作台自定义控件：环形进度、横向柱状图、Toast、Agent 卡片、
// 告警卡片、可折叠区块卡片。全部 QPainter / 原生 widget 实现，无 QML。
#include <QBrush>
#include <QConicalGradient>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidget>

#include "theme.h"
#include "i18n.h"

namespace ui {

// 卡片悬停高亮：进入边框变强调色（AgentCard / 通用 QFrame 卡片）
inline void hoverGlow(QFrame* frame, const char* objectName) {
    frame->setAttribute(Qt::WA_Hover);
    frame->setStyleSheet(th(QString("QFrame#%1 { background:@card@; border:1px solid @line@;"
                           " border-radius:8px; }"
                           "QFrame#%1:hover { background:@fieldhover@; border:1px solid @accent@;"
                           " border-radius:8px; }").arg(objectName)));
}

// 状态胶囊（在线/离线徽标）：主题语义色淡染底 + 亮色文字
inline QString pillStyle(const QColor& c, int alpha = 50) {
    return QString("font-size:10px; padding:1px 8px; border-radius:8px;"
                   " color:%1; background:rgba(%2,%3,%4,%5);")
        .arg(c.lighter(140).name())
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(alpha);
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
        QPen pen(line(), 12, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.drawArc(rect, 45 * 16, -270 * 16);
        // 进度环：外圈柔光营造发光质感，主弧沿环锥形渐变（亮端→本色）；
        // 占比驱动颜色（蓝→蜜金→红）
        QColor c = usageColor(ratio);
        if (ratio > 0.001) {
            QPen glow(QColor(c.red(), c.green(), c.blue(), 46), 22, Qt::SolidLine, Qt::RoundCap);
            p.setPen(glow);
            p.drawArc(rect, 45 * 16, int(-270 * 16 * ratio));
            QConicalGradient sheen(rect.center(), 90);
            sheen.setColorAt(0.0, c.lighter(150));
            sheen.setColorAt(1.0, c);
            QPen prog(QBrush(sheen), 12, Qt::SolidLine, Qt::RoundCap);
            p.setPen(prog);
            p.drawArc(rect, 45 * 16, int(-270 * 16 * ratio));
        }
        // 中心文字（百分比随用量着色，与环体呼应）
        p.setPen(QPen(usageColor(ratio).lighter(115)));
        QFont f = p.font();
        f.setPixelSize(side / 6);
        f.setBold(true);
        p.setFont(f);
        QRectF center = rect.adjusted(20, 20, -20, -20);
        p.drawText(center.adjusted(0, -14, 0, -14), Qt::AlignCenter,
                   QString("%1%").arg(ratio * 100, 0, 'f', 1));
        f.setPixelSize(side / 14);
        p.setFont(f);
        p.setPen(QPen(muted()));
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
            p.setPen(QPen(muted()));
            p.drawText(rect(), Qt::AlignCenter, i18n::trs("暂无用量数据", "no usage data yet"));
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
            p.setPen(QPen(text()));
            p.drawText(QRect(0, y, labelW - 4, rowH), Qt::AlignVCenter | Qt::AlignRight,
                       entries_[i].first);
            // 底槽
            p.setPen(Qt::NoPen);
            p.setBrush(line());
            p.drawRoundedRect(QRect(barX, y + rowH / 2 - 5, barW, 10), 5, 5);
            // 柱体：最大值高亮蓝，其余随占比变暗；横向渐变（本色→亮）更有质感；
            // 宽度乘以入场扫掠进度
            double ratio = double(entries_[i].second) / double(maxV) * sweep_;
            int w = int(double(barW) * ratio);
            QColor bar = accent();
            if (entries_[i].second != maxV) {
                bar = accent().darker(100 + int((1.0 - ratio) * 90));
                bar.setAlpha(210);
            }
            QLinearGradient sheen(barX, 0, barX + qMax(w, 4), 0);
            sheen.setColorAt(0.0, bar);
            sheen.setColorAt(1.0, bar.lighter(140));
            p.setBrush(sheen);
            p.drawRoundedRect(QRect(barX, y + rowH / 2 - 5, qMax(w, 4), 10), 5, 5);
            // 数值 + 占比（等宽）
            QFont mf = p.font();
            mf.setFamily(mono());
            p.setFont(mf);
            p.setPen(QPen(muted()));
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
        setStyleSheet(th(success
            ? "QLabel#toastOk { background:@okbg@; color:@ok@; border:1px solid @ok@;"
              " border-radius:8px; padding:10px 18px; font-size:12px; }"
            : "QLabel#toastErr { background:@errbg@; color:@danger@; border:1px solid @danger@;"
              " border-radius:8px; padding:10px 18px; font-size:12px; }"));
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
        status_->setStyleSheet(pillStyle(ok()));
        role_ = new QLabel(this);
        role_->setStyleSheet(th("color:@muted@; font-size:11px; background:transparent;"));
        head->addWidget(dot_);
        head->addWidget(name_);
        head->addWidget(status_);
        head->addStretch(1);
        head->addWidget(role_);
        lay->addLayout(head);
        task_ = new QLabel(this);
        task_->setStyleSheet(th("color:@text@; font-size:11px; background:transparent;"));
        task_->setWordWrap(true);
        lay->addWidget(task_);
        seen_ = new QLabel(this);
        seen_->setStyleSheet(
            th("color:@muted@; font-size:10px; font-family:@mono@,monospace; background:transparent;"));
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
        status_->setText(online_ ? i18n::trs("在线", "online") : i18n::trs("离线", "offline"));
        status_->setStyleSheet(online_ ? pillStyle(ok()) : pillStyle(muted(), 40));
        role_->setText(role);
        task_->setText(task.isEmpty() ? i18n::trs("（无当前任务）", "(no current task)") : task);
        seen_->setText(lastSeen.isEmpty() ? i18n::trs("从未活跃", "never seen")
                                      : i18n::trs("活跃于 ", "last active ") + lastSeen);
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
                                    .arg(pulseOn_ ? ok().name() : ok().darker(150).name())
                              : QString("<span style='color:%1;'>●</span>")
                                    .arg(muted().name()));
    }
};

// ---- 告警卡片：红=阻断 / 橙=警告 / 黄=注意 ----
class AlertCard : public QFrame {
public:
    AlertCard(const QString& severity, const QString& text, QWidget* parent = nullptr)
        : QFrame(parent) {
        QColor c = severity == "critical" ? danger() : (severity == "warn" ? warn() : note());
        // 卡底按严重度淡染（12% 透明度），左侧色条 + 同色文字
        setStyleSheet(QString("QFrame { background:rgba(%1,%2,%3,28); border-left:4px solid %4;"
                              " border-radius:6px; padding:2px; }")
                          .arg(c.red())
                          .arg(c.green())
                          .arg(c.blue())
                          .arg(c.name()));
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
        toggle->setStyleSheet(th(
            "QToolButton { background:@card@; border:1px solid @line@;"
            " border-radius:8px; padding:10px 14px; font-size:13px; font-weight:600;"
            " text-align:left; }"
            "QToolButton:hover { border-color:@accent@; }"));
        content_->setStyleSheet(th(
            "QWidget { background:@field@; border:1px solid @line@;"
            " border-top:none; border-radius:0 0 8px 8px; }"));
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
