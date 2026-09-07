#pragma once
// 深色主题色板与字体规范（与 dark.qss 保持一致）。
#include <QColor>
#include <QString>

namespace ui {

inline const QColor BG("#1e1e1e");        // 窗口背景
inline const QColor CARD("#2d2d2d");      // 卡片背景
inline const QColor CARD_LINE("#3f3f46"); // 卡片描边
inline const QColor ACCENT("#0ea5e9");    // 强调蓝
inline const QColor OK("#22c55e");        // 成功绿 / 在线
inline const QColor DANGER("#ef4444");    // 阻断红
inline const QColor WARN("#f59e0b");      // 警告橙
inline const QColor NOTE("#eab308");      // 注意黄
inline const QColor TEXT("#e5e5e5");      // 主文本
inline const QColor MUTED("#9ca3af");     // 次要文本

inline QString mono() { return QStringLiteral("Consolas"); }
inline QString sans() { return QStringLiteral("Segoe UI, Microsoft YaHei UI"); }

// 用量占比 → 颜色（预算临近渐变：<80% 蓝，<95% 橙，其余红）
inline QColor usageColor(double ratio) {
    if (ratio < 0.80) return ACCENT;
    if (ratio < 0.95) return WARN;
    return DANGER;
}

}  // namespace ui
