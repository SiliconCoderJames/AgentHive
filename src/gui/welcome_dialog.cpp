#include "welcome_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "i18n.h"
#include "theme.h"

WelcomeDialog::WelcomeDialog(zp::Platform& platform, QWidget* parent) : QDialog(parent) {
    setWindowTitle(i18n::trs("欢迎来到 AgentHive", "Welcome to AgentHive"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    // 不锁死尺寸：字号调到 14px 或系统字体放大时，固定尺寸会把内容挤掉
    setMinimumSize(620, 540);
    resize(620, 540);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(8);

    // 品牌头：logo + 名称 + 定稿口号
    auto* logo = new QLabel(this);
    QPixmap pm(":/brand/logo.png");
    logo->setPixmap(pm.scaled(88, 88, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);
    root->addWidget(logo);
    auto* name = new QLabel("AgentHive", this);
    name->setAlignment(Qt::AlignCenter);
    name->setStyleSheet(ui::th("font-size:24px; font-weight:800; color:@text@;"));
    root->addWidget(name);
    auto* slogan = new QLabel(this);
    slogan->setText(QString("%1  ·  %2")
                        .arg(i18n::trs("单体成长，蜂巢共享", "Grow alone, thrive together."),
                             i18n::trs("本地优先，数据不出本机", "local-first, data stays local")));
    slogan->setAlignment(Qt::AlignCenter);
    slogan->setStyleSheet(ui::th("font-size:12px; color:@brand@; font-weight:600;"));
    root->addWidget(slogan);
    root->addSpacing(10);

    // 三步接入卡片（图标用与侧栏同源的矢量图标，替代 emoji：跨平台字形一致）
    struct Step {
        const char* kind;
        const char* zh;
        const char* en;
        const char* zhDesc;
        const char* enDesc;
    };
    const Step steps[] = {
        {"overview", "蜂巢已就绪", "Hive is ready", "工作台内置本地服务，127.0.0.1 仅本机可达",
         "The workbench ships a local-only service on 127.0.0.1"},
        {"key", "接入你的 Agent", "Connect your agents",
         "复制下方命令，在任意 Agent 的终端执行即可入巢", "Copy the command and run it in any agent's terminal"},
        {"knowledge", "共享与成长", "Share and grow",
         "经验进知识库、技能进市场，所有 Agent 越用越顺",
         "Knowledge and skills are shared by every agent"},
    };
    for (const auto& s : steps) {
        auto* row = new QHBoxLayout();
        row->setSpacing(12);
        auto* icon = new QLabel(this);
        icon->setPixmap(ui::makeIcon(s.kind, ui::brand(), 22).pixmap(22, 22));
        icon->setStyleSheet("background:transparent;");
        icon->setFixedWidth(30);
        auto* text = new QLabel(this);
        text->setTextFormat(Qt::RichText);
        text->setText(ui::th(QString(
            "<b style='color:@text@; font-size:13px;'>%1</b><br>"
            "<span style='color:@muted@; font-size:11px;'>%2</span>")
                                 .arg(i18n::trs(s.zh, s.en))
                                 .arg(i18n::trs(s.zhDesc, s.enDesc))));
        text->setWordWrap(true);
        row->addWidget(icon, 0, Qt::AlignTop);
        row->addWidget(text, 1);
        root->addLayout(row);
    }
    root->addSpacing(8);

    // 注册命令（不含密钥本体，路径指向本机主密钥文件）
    auto* cmdTitle = new QLabel(this);
    cmdTitle->setText(i18n::trs("接入命令（首个 Agent）", "Connect command (first agent)"));
    cmdTitle->setStyleSheet(ui::th("font-size:11px; color:@muted@;"));
    root->addWidget(cmdTitle);
    auto* cmdRow = new QHBoxLayout();
    auto* cmd = new QLabel(this);
    const QString cmdText =
        QString("agent-cli register --name myagent --master-key "
                "(Get-Content \"%1/config/master.key\")")
            .arg(QString::fromStdString(platform.homeDir()));
    cmd->setText(ui::th(QString("<span style='color:@accenthi@; font-family:@mono@,monospace; "
                                "font-size:11px;'>%1</span>")
                            .arg(cmdText.toHtmlEscaped())));
    cmd->setWordWrap(true);
    cmdRow->addWidget(cmd, 1);
    auto* copyBtn = new QToolButton(this);
    copyBtn->setText(i18n::trs("复制", "Copy"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QToolButton::clicked, this, [cmdText, this] {
        QApplication::clipboard()->setText(cmdText);
        ui::Toast::show(this, i18n::trs("已复制", "Copied"));
    });
    cmdRow->addWidget(copyBtn);
    root->addLayout(cmdRow);
    root->addSpacing(6);

    // 主题色卡：第一印象自己选
    auto* themeTitle = new QLabel(this);
    themeTitle->setText(i18n::trs("选一套配色（随时在设置里换）",
                                  "Pick a theme (change anytime in Settings)"));
    themeTitle->setStyleSheet(ui::th("font-size:11px; color:@muted@;"));
    root->addWidget(themeTitle);
    auto* grid = new QGridLayout();
    grid->setSpacing(8);
    for (int i = 0; i < static_cast<int>(ui::themes().size()); ++i) {
        const auto& t = ui::themes()[static_cast<std::size_t>(i)];
        auto* sw = new ui::ThemeSwatch(i18n::trs(t.zh, t.en), t.bg, t.deep, t.accent, t.brand,
                                       t.text, [i] { ui::setThemeIndex(i); }, this);
        sw->setFixedSize(92, 68);
        swatches_.push_back(sw);
        grid->addWidget(sw, i / 5, i % 5);
    }
    root->addLayout(grid);
    root->addStretch(1);

    auto* row = new QHBoxLayout();
    row->addStretch(1);
    auto* go = new QPushButton(i18n::trs("开始使用 →", "Get started →"), this);
    go->setObjectName("primary");
    go->setFixedHeight(34);
    go->setFixedWidth(140);
    connect(go, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(go);
    root->addLayout(row);

    connect(this, &QDialog::finished, this, [](int) {
        QSettings s;
        s.setValue("ui/welcomeSeen", true);
    });
}
