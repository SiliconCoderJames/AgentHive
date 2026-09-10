#pragma once
// 态势感知工作台自定义控件：环形进度、横向/纵向柱状图、主题色卡、Toast、
// Agent 卡片、告警卡片、可折叠区块卡片。全部 QPainter / 原生 widget 实现，无 QML。
#include <QBrush>
#include <QConicalGradient>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QIcon>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidget>

#include <cmath>
#include <functional>

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
            // 名称过长省略号收尾，避免与柱体重叠
            QFontMetrics fm(p.font());
            QString label = fm.elidedText(entries_[i].first, Qt::ElideRight, labelW - 8);
            p.setPen(QPen(text()));
            p.drawText(QRect(0, y, labelW - 4, rowH), Qt::AlignVCenter | Qt::AlignRight, label);
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

// 紧凑数字：<1万原样，1万~100万 "12.3k"，≥100万 "1.23M"（柱顶标注用）
inline QString fmtCompact(qint64 v) {
    if (v < 10000) return QString::number(v);
    const bool mega = v >= 1000000;
    double x = mega ? double(v) / 1000000.0 : double(v) / 1000.0;
    QString s = QString::number(x, 'f', x < 100 ? 1 : 0);
    while (s.contains('.') && s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
    return s + (mega ? "M" : "k");
}

// ---- 主题色卡：迷你界面预览（底色/侧栏/文本线/强调块/品牌点），点击选择主题 ----
class ThemeSwatch : public QFrame {
public:
    ThemeSwatch(const QString& name, QColor bg, QColor deep, QColor accent, QColor brand,
                QColor text, std::function<void()> onClick, QWidget* parent = nullptr)
        : QFrame(parent), name_(std::move(name)), bg_(std::move(bg)), deep_(std::move(deep)),
          accent_(std::move(accent)), brand_(std::move(brand)), text_(std::move(text)),
          onClick_(std::move(onClick)) {
        setFixedSize(108, 78);
        setCursor(Qt::PointingHandCursor);
    }

    void setSelected(bool on) {
        if (selected_ == on) return;
        selected_ = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        // 卡片底 = 主题窗口底色
        p.setPen(Qt::NoPen);
        p.setBrush(bg_);
        p.drawRoundedRect(r, 9, 9);
        // 左侧侧栏条 + 右侧内容示意线
        p.setBrush(deep_);
        p.drawRoundedRect(QRectF(7, 7, 20, height() - 26), 4, 4);
        p.setPen(QPen(QColor(255, 255, 255, 36), 3, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(34, 13 + i * 9), QPointF(width() - 10.0, 13 + i * 9));
        p.setPen(Qt::NoPen);
        // 强调色块 + 品牌色圆点
        p.setBrush(accent_);
        p.drawRoundedRect(QRectF(34, height() - 26, 30, 8), 3, 3);
        p.setBrush(brand_);
        p.drawEllipse(QPointF(width() - 16, height() - 22), 5, 5);
        // 名称
        p.setPen(QPen(text_));
        QFont f = p.font();
        f.setPixelSize(10);
        p.setFont(f);
        p.drawText(QRectF(0, height() - 17, width(), 15), Qt::AlignCenter, name_);
        // 边框：选中 = 强调色 + 对勾；悬停 = 微亮
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(selected_ ? accent_ : QColor(255, 255, 255, hovered_ ? 70 : 26),
                      selected_ ? 2 : 1));
        p.drawRoundedRect(r, 9, 9);
        if (selected_) {
            p.setPen(QPen(accent_));
            QFont bf = p.font();
            bf.setPixelSize(11);
            bf.setBold(true);
            p.setFont(bf);
            p.drawText(QRectF(0, 4, width() - 6, 14), Qt::AlignRight, "✓");
        }
    }
    void mousePressEvent(QMouseEvent*) override {
        if (onClick_) onClick_();
    }
    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        update();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }

private:
    QString name_;
    QColor bg_, deep_, accent_, brand_, text_;
    std::function<void()> onClick_;
    bool selected_ = false;
    bool hovered_ = false;
};

// ---- 纵向柱状图：每日 Token 趋势，柱体自底部扫掠升起 ----
// 峰值柱用品牌色蜜金 + 亮色数值，其余强调色纵向渐变；底部日期 MM-DD。
class VBarChart : public QWidget {
public:
    explicit VBarChart(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(170);
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
        const int valueH = 18;  // 顶部数值标签区
        const int labelH = 20;  // 底部日期标签区
        QRectF plot(2, valueH, width() - 4, height() - valueH - labelH);
        qint64 maxV = 1;
        for (const auto& e : entries_) maxV = qMax(maxV, e.second);
        // 网格：顶/中/底三条淡虚线
        p.setPen(QPen(line(), 1, Qt::DashLine));
        for (double r : {0.0, 0.5, 1.0}) {
            double y = plot.bottom() - plot.height() * r;
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        const int n = entries_.size();
        const double slot = plot.width() / n;
        const double barW = qMin(26.0, slot * 0.62);
        QFont f = p.font();
        f.setPixelSize(9);
        p.setFont(f);
        for (int i = 0; i < n; ++i) {
            double cx = plot.left() + slot * (i + 0.5);
            double ratio = double(entries_[i].second) / double(maxV) * sweep_;
            double h = plot.height() * ratio;
            QRectF bar(cx - barW / 2, plot.bottom() - h, barW, h);
            bool isMax = entries_[i].second == maxV && maxV > 1;
            QColor base = isMax ? brand() : accent();
            QLinearGradient sheen(bar.topLeft(), bar.bottomLeft());
            sheen.setColorAt(0.0, base.lighter(135));
            sheen.setColorAt(1.0, base.darker(108));
            p.setPen(Qt::NoPen);
            p.setBrush(sheen);
            if (h > 0.5) p.drawRoundedRect(bar, 3, 3);
            // 顶部数值（峰值亮色，其余弱化）
            p.setPen(QPen(isMax ? brand() : muted()));
            p.drawText(QRectF(cx - slot / 2, bar.top() - valueH + 2, slot, valueH),
                       Qt::AlignCenter, fmtCompact(entries_[i].second));
            // 底部日期 MM-DD
            QString day = entries_[i].first;
            if (day.size() >= 10) day = day.mid(5);
            p.setPen(QPen(muted()));
            p.drawText(QRectF(cx - slot / 2, plot.bottom() + 3, slot, labelH - 3),
                       Qt::AlignCenter, day);
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

// ---- 空状态母题：蜜金蜂巢三六边形 + 标题 + 出路提示（品牌触点，见 docs/brand.md §3）----
class HexEmptyState : public QWidget {
public:
    HexEmptyState(const QString& emoji, const QString& title, const QString& hint,
                  QWidget* parent = nullptr)
        : QWidget(parent), emoji_(emoji), title_(title), hint_(hint) {
        setMinimumHeight(180);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width() / 2.0, 64.0);
        const qreal r = 22.0;
        // 蜂巢三六边形：共享边拼接，描边圆角连接（与 logo 同构，浅描边弱化）
        QPen pen(brand(), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pen.setColor(QColor(brand().red(), brand().green(), brand().blue(), 130));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        auto hex = [&](const QPointF& ctr) {
            QPolygonF h;
            for (int i = 0; i < 6; ++i) {
                qreal a = M_PI / 180.0 * (60.0 * i - 30.0);
                h << ctr + QPointF(r * std::cos(a), r * std::sin(a));
            }
            p.drawPolygon(h);
        };
        hex(c + QPointF(0, -r * 1.02));
        hex(c + QPointF(-r * 0.9, r * 0.55));
        hex(c + QPointF(r * 0.9, r * 0.55));
        p.setPen(Qt::NoPen);
        p.setBrush(accent());
        p.drawEllipse(c + QPointF(0, -r * 1.02), r * 0.3, r * 0.3);
        // emoji + 标题 + 出路提示
        p.setPen(QPen(text()));
        QFont f = p.font();
        f.setPixelSize(26);
        p.setFont(f);
        p.drawText(QRect(0, 96, width(), 34), Qt::AlignCenter, emoji_);
        f.setPixelSize(13);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 134, width(), 20), Qt::AlignCenter, title_);
        f.setPixelSize(11);
        f.setBold(false);
        p.setFont(f);
        p.setPen(QPen(muted()));
        p.drawText(QRect(24, 156, width() - 48, 40), Qt::AlignHCenter | Qt::TextWordWrap, hint_);
    }

private:
    QString emoji_, title_, hint_;
};

// 蜂巢母题注册为文档图片资源：HTML 空状态里 <img src="hexmotif"> 引用，
// 颜色随当前主题（蜜金描边 + 强调色入口点，见 docs/brand.md §3）
inline void attachHexMotif(QTextDocument* doc) {
    const int w = 96, h = 70, r = 15;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(brand().red(), brand().green(), brand().blue(), 128), 3.5, Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto hex = [&](const QPointF& c) {
        QPolygonF poly;
        for (int i = 0; i < 6; ++i) {
            qreal a = M_PI / 180.0 * (60.0 * i - 30.0);
            poly << c + QPointF(r * std::cos(a), r * std::sin(a));
        }
        p.drawPolygon(poly);
    };
    const QPointF top(w / 2.0, 30.0);
    hex(top);
    hex(top + QPointF(-r * 0.9 * 1.68, r * 1.34));
    hex(top + QPointF(r * 0.9 * 1.68, r * 1.34));
    p.setPen(Qt::NoPen);
    p.setBrush(accent());
    p.drawEllipse(top, r * 0.3, r * 0.3);
    p.end();
    doc->addResource(QTextDocument::ImageResource, QUrl("hexmotif"), pm);
}

// ---- 程序化线性图标：统一 2px 圆角描边、随主题着色，替代大小不一的 emoji ----
// kind: overview / knowledge / skills / memory / messages / errors / audit / gear
inline QIcon makeIcon(const QString& kind, const QColor& color, int px = 18,
                      const QColor& selectedColor = {}) {
    auto paint = [&](QPixmap& pm, const QColor& c) {
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(pm.width() / 24.0, pm.height() / 24.0);
        QPen pen(c, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        auto poly = [&](std::initializer_list<QPointF> pts, bool close) {
            QPolygonF f;
            for (const auto& q : pts) f << q;
            if (close)
                p.drawPolygon(f);
            else
                p.drawPolyline(f);
        };
        if (kind == "overview") {  // 六边形 + 入口点（品牌母题）
            poly({{12, 3}, {20, 7.5}, {20, 16.5}, {12, 21}, {4, 16.5}, {4, 7.5}}, true);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(QPointF(12, 12), 2.4, 2.4);
        } else if (kind == "knowledge") {  // 摊开的书
            poly({{4, 5},
                  {8, 4},
                  {12, 6},
                  {16, 4},
                  {20, 5},
                  {20, 18},
                  {15, 16.5},
                  {12, 17.5},
                  {9, 16.5},
                  {4, 18}},
                 true);
            p.drawLine(QPointF(12, 6), QPointF(12, 17.5));
        } else if (kind == "skills") {  // 能量螺栓
            poly({{13, 2}, {5, 14}, {11, 14}, {9, 22}, {19, 10}, {13, 10}}, true);
        } else if (kind == "memory") {  // 分层（对应记忆分层设计）
            poly({{12, 3}, {21, 8}, {12, 13}, {3, 8}}, true);
            poly({{3, 12}, {12, 17}, {21, 12}}, false);
            poly({{3, 16}, {12, 21}, {21, 16}}, false);
        } else if (kind == "messages") {  // 对话气泡
            p.drawRoundedRect(QRectF(3, 4, 18, 12), 3.5, 3.5);
            poly({{8, 16}, {8, 20}, {12, 16}}, false);
        } else if (kind == "errors") {  // 警示三角
            poly({{12, 3}, {22, 20}, {2, 20}}, true);
            p.drawLine(QPointF(12, 9.5), QPointF(12, 14.5));
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(QPointF(12, 17.2), 1.15, 1.15);
        } else if (kind == "audit") {  // 时钟
            p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
            p.drawLine(QPointF(12, 7.5), QPointF(12, 12));
            p.drawLine(QPointF(12, 12), QPointF(15, 14));
        } else if (kind == "gear") {  // 设置齿轮
            p.drawEllipse(QPointF(12, 12), 6.2, 6.2);
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(12, 12), 2.1, 2.1);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            for (int i = 0; i < 8; ++i) {
                qreal a = M_PI / 4.0 * i;
                p.drawLine(QPointF(12 + 6.4 * std::cos(a), 12 + 6.4 * std::sin(a)),
                           QPointF(12 + 9.2 * std::cos(a), 12 + 9.2 * std::sin(a)));
            }
        }
    };
    QIcon icon;
    QPixmap pm(px * 2, px * 2);
    pm.setDevicePixelRatio(2);
    paint(pm, color);
    icon.addPixmap(pm);
    if (selectedColor.isValid()) {
        QPixmap ps(px * 2, px * 2);
        ps.setDevicePixelRatio(2);
        paint(ps, selectedColor);
        icon.addPixmap(ps, QIcon::Selected);
    }
    return icon;
}

}  // namespace ui
