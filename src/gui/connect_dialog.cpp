#include "connect_dialog.h"

#include <QClipboard>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "i18n.h"
#include "widgets.h"

namespace {

// 工作台 HTTP 端口与 core 的取值顺序一致（MIDERHIVE_PORT 优先，旧名兜底，默认 8787）
QString baseUrl() {
    QString fromEnv = qEnvironmentVariable("MIDERHIVE_PORT");
    if (fromEnv.isEmpty()) fromEnv = qEnvironmentVariable("AGENTHIVE_PORT");
    if (fromEnv.isEmpty()) fromEnv = qEnvironmentVariable("ZCODE_PLATFORM_PORT");
    const QString port = fromEnv.isEmpty() ? QStringLiteral("8787") : fromEnv;
    return QStringLiteral("http://127.0.0.1:") + port;
}

}  // namespace

ConnectDialog::ConnectDialog(ah::Platform& platform, QWidget* parent)
    : QDialog(parent), platform_(platform) {
    setWindowTitle(i18n::trs("接入常用 Agent", "Connect common agents"));
    setModal(true);
    resize(760, 560);

    // 与用户点名顺序一致；zcode 是保留名（管理者身份），ZCode 工具接入用 zcode-agent
    presets_ = {
        {"ZCode", "zcode-agent",
         "ZCode 的执行器通过环境变量接入（agent-cli / platformd 已内置支持）："
         "设置 MIDERHIVE_AGENT_NAME、MIDERHIVE_AGENT_KEY、MIDERHIVE_PORT 后即可自动心跳上线。",
         "ZCode's runner connects via environment variables (built into agent-cli / "
         "platformd): set MIDERHIVE_AGENT_NAME, MIDERHIVE_AGENT_KEY and MIDERHIVE_PORT."},
        {"Codex / ChatGPT", "codex",
         "把生成的指令块粘贴到项目的 AGENTS.md（或 ~/.codex/AGENTS.md），让 Codex 在任务中"
         "按该说明调用本工作台 HTTP API。",
         "Paste the generated block into AGENTS.md in your project (or ~/.codex/AGENTS.md) "
         "so Codex follows it when calling this workbench's HTTP API."},
        {"Claude Code", "claude",
         "把生成的指令块粘贴到项目根的 CLAUDE.md（或 ~/.claude/CLAUDE.md），Claude Code 会"
         "作为项目记忆读入。",
         "Paste the generated block into CLAUDE.md at the project root (or "
         "~/.claude/CLAUDE.md); Claude Code reads it as project memory."},
        {"Factory Droid", "droid",
         "Factory CLI 遵循 AGENTS.md 约定：把指令块粘贴到项目根的 AGENTS.md。",
         "Factory CLI follows the AGENTS.md convention: paste the block into AGENTS.md at "
         "the project root."},
        {"Hermes Agent", "hermes",
         "通用 HTTP 接入：把凭据写入 Hermes 的配置或启动环境，然后按指令块调用 API。",
         "Generic HTTP integration: put the credentials into Hermes' config or startup "
         "environment, then call the API as described in the block."},
        {"Cursor", "cursor",
         "把生成的指令块粘贴到 .cursor/rules/miderhive.mdc（项目规则），Cursor 会按规则"
         "读入。",
         "Paste the generated block into .cursor/rules/miderhive.mdc (Project Rules); "
         "Cursor picks rules up from there."},
        {"GitHub Copilot", "copilot",
         "把生成的指令块粘贴到 .github/copilot-instructions.md（仓库自定义指令）。",
         "Paste the generated block into .github/copilot-instructions.md (custom "
         "instructions for your repository)."},
    };

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(22, 20, 22, 18);
    lay->setSpacing(12);

    auto* intro =
        new QLabel(i18n::trs("选择要接入的工具，一键签发凭据并生成可粘贴的接入配置。"
                             "接入方式以各工具官方文档为准，本页只生成统一的凭据与指令块。",
                             "Pick a tool, issue credentials in one click, and get a ready-to-paste "
                             "setup block. Each tool's exact wiring follows its own docs; this page "
                             "only generates unified credentials and instructions."),
                   this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto* body = new QHBoxLayout();
    body->setSpacing(14);

    list_ = new QListWidget(this);
    list_->setFixedWidth(190);
    for (const Preset& p : presets_) list_->addItem(QString::fromUtf8(p.label));
    list_->setCurrentRow(0);
    body->addWidget(list_);

    auto* right = new QVBoxLayout();
    right->setSpacing(8);

    desc_ = new QLabel(this);
    desc_->setWordWrap(true);
    right->addWidget(desc_);

    auto* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(i18n::trs("Agent 名称", "Agent name"), this));
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setFixedWidth(180);
    nameRow->addWidget(nameEdit_);
    nameRow->addStretch(1);
    right->addLayout(nameRow);

    provisionBtn_ = new QPushButton(i18n::trs("一键接入（签发新密钥）", "Connect (issue new key)"),
                                    this);
    right->addWidget(provisionBtn_);

    snippet_ = new QPlainTextEdit(this);
    snippet_->setReadOnly(true);
    snippet_->setPlaceholderText(
        i18n::trs("点击「一键接入」后，这里生成接入配置……",
                  "Click \"Connect\" to generate the setup block…"));
    right->addWidget(snippet_, 1);

    auto* copyBtn = new QPushButton(i18n::trs("复制全部", "Copy all"), this);
    right->addWidget(copyBtn);

    state_ = new QLabel(this);
    state_->setWordWrap(true);
    right->addWidget(state_);

    body->addLayout(right, 1);
    lay->addLayout(body, 1);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) { applyPreset(row); });
    connect(provisionBtn_, &QPushButton::clicked, this, [this] { provision(); });
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(snippet_->toPlainText());
        ui::Toast::show(this, i18n::trs("已复制到剪贴板 ✓", "Copied to clipboard ✓"));
    });

    applyPreset(0);
}

void ConnectDialog::applyPreset(int row) {
    if (row < 0 || row >= presets_.size()) return;
    const Preset& p = presets_[row];
    desc_->setText(i18n::trs(QString::fromUtf8(p.zhHint), QString::fromUtf8(p.enHint)));
    nameEdit_->setText(QString::fromUtf8(p.agentName));
    state_->clear();
}

void ConnectDialog::provision() {
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        ui::Toast::show(this, i18n::trs("请先填写 Agent 名称", "Enter an agent name first"), false);
        return;
    }
    std::string key, err;
    if (!platform_.agentProvision(ah::kManagerName, name.toStdString(), key, err)) {
        state_->setText(i18n::trs("签发失败：", "Failed: ") + QString::fromStdString(err));
        return;
    }
    const int row = list_->currentRow();
    const Preset& p = presets_[row < 0 ? 0 : row];
    snippet_->setPlainText(buildSnippet(QString::fromUtf8(p.label), name,
                                        QString::fromStdString(key),
                                        i18n::trs(QString::fromUtf8(p.zhHint),
                                                  QString::fromUtf8(p.enHint))));
    QApplication::clipboard()->setText(snippet_->toPlainText());
    state_->setText(i18n::trs(
        "已签发新密钥 ✓（旧密钥立即失效）。接入配置已复制到剪贴板；新密钥仅显示这一次，"
        "丢失后可在 设置 → Agent 管理 重新生成。",
        "New key issued ✓ (the old one stops working). The setup block is on your clipboard; "
        "the key is shown only this once — if lost, re-issue it from Settings → Agents."));
    ui::Toast::show(this, i18n::trs("一键接入完成 ✓", "Connected ✓"));
}

QString ConnectDialog::buildSnippet(const QString& tool, const QString& name, const QString& key,
                                    const QString& hint) const {
    const QString url = baseUrl();
    return QStringLiteral(
               "# Connect %1 to MiderHive\n"
               "\n"
               "Base URL : %2\n"
               "Agent    : %3\n"
               "API key  : %4\n"
               "           (shown only once; re-issue from Settings > Agents if lost)\n"
               "\n"
               "## 1) Send these headers on every request\n"
               "X-Agent-Name: %3\n"
               "X-Api-Key: %4\n"
               "\n"
               "## 2) Heartbeat every 60s so the workbench shows you as online\n"
               "##    (an agent is marked offline after 120s without a heartbeat)\n"
               "curl -X POST %2/api/agents/heartbeat \\\n"
               "  -H \"X-Agent-Name: %3\" -H \"X-Api-Key: %4\" \\\n"
               "  -H \"Content-Type: application/json\" \\\n"
               "  -d \"{\\\"current_task\\\":\\\"working on ...\\\"}\"\n"
               "\n"
               "## 3) Collaborate: list teammates\n"
               "curl %2/api/agents -H \"X-Agent-Name: %3\" -H \"X-Api-Key: %4\"\n"
               "\n"
               "## Full endpoint reference: MiderHive > Settings > Agent API\n"
               "\n"
               "## Where to put this\n"
               "%5\n")
        .arg(tool, url, name, key, hint);
}
