#pragma once
// 首次运行引导：蜂巢是什么 → 三步接入（可复制注册命令）→ 选主题色卡。
// 关闭时由调用方写入 ui/welcomeSeen，此后不再弹出（可在 ⚙ 设置里随时改外观）。
#include <QDialog>
#include <vector>

#include "core/platform.h"
#include "widgets.h"

class QComboBox;

class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeDialog(ah::Platform& platform, QWidget* parent = nullptr);

private:
    std::vector<ui::ThemeSwatch*> swatches_;
};
