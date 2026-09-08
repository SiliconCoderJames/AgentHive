#pragma once
// 轻量 i18n：zh-CN 为源语言，en 为第二语言；运行时切换并持久化到 QSettings。
// 覆盖范围按批次推进（当前：侧边栏/导航/页头/状态栏等铬层），
// 面板深层控件文案在后续批次逐步接入 tr()/trs()。
#include <QSettings>
#include <QString>
#include <functional>
#include <vector>

namespace i18n {

enum class Lang { Zh, En };

inline Lang g_lang = Lang::Zh;
inline std::vector<std::function<void()>>& listeners() {
    static std::vector<std::function<void()>> v;
    return v;
}

inline void load() {
    QSettings s("agenthive", "agenthive");
    g_lang = s.value("ui/lang", "zh").toString() == "en" ? Lang::En : Lang::Zh;
}

inline void apply(Lang l) {
    g_lang = l;
    QSettings s("agenthive", "agenthive");
    s.setValue("ui/lang", l == Lang::En ? "en" : "zh");
    for (auto& f : listeners()) f();
}

inline void toggle() { apply(g_lang == Lang::Zh ? Lang::En : Lang::Zh); }

// 双语取词：zh 为源语言文案，en 为英文对照
inline const char* tr(const char* zh, const char* en) {
    return g_lang == Lang::Zh ? zh : en;
}
inline QString trs(const QString& zh, const QString& en) {
    return g_lang == Lang::Zh ? zh : en;
}

}  // namespace i18n
