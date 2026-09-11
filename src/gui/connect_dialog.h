#pragma once
// 接入向导：常用 Agent/ Harness 的一键接入。
// 左侧选择目标工具，右侧生成可直接粘贴的凭据与指令块；
// "一键接入"幂等：该 Agent 不存在则注册，已存在则轮换密钥（明文不可恢复，
// 因此每次接入都会签发新钥并覆盖剪贴板/文件）。
#include <QDialog>
#include <QLabel>
#include <QString>
#include <QVector>

#include "core/platform.h"

class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectDialog(ah::Platform& platform, QWidget* parent = nullptr);

private:
    struct Preset {
        const char* label;      // 工具显示名
        const char* agentName;  // 默认注册名（保留名不可用，故 ZCode 用 zcode-agent）
        const char* zhHint;     // 该工具的接入落点说明（中文）
        const char* enHint;     // 同上（英文）
    };

    void applyPreset(int row);
    void provision();
    QString buildSnippet(const QString& tool, const QString& name, const QString& key,
                         const QString& hint) const;

    ah::Platform& platform_;
    QListWidget* list_ = nullptr;
    QLabel* desc_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QPushButton* provisionBtn_ = nullptr;
    QPlainTextEdit* snippet_ = nullptr;
    QLabel* state_ = nullptr;
    QVector<Preset> presets_;
};
