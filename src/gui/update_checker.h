#pragma once
// 应用内更新：从 GitHub Releases 抓最新版并安装。
//
// 为什么走 release 资产里的 latest.json 而不是 GitHub API：
//   * 固定 URL https://github.com/<repo>/releases/latest/download/latest.json 可直接下载，
//     不需要 token，也不受未认证 API「每小时 60 次」的限流；
//   * 清单里带每个产物的 SHA256，正是安装前要校验的东西；
//   * `releases/latest` 天然排除预发布版本，不会把 rc 推给正式版用户。
//
// 安全取向（刻意的取舍，不是没做完）：
//   * **默认不静默安装**。本程序持有用户全部协作数据、且当前产物未代码签名，
//     静默替换可执行文件的风险远大于收益；这里做的是"自动检查 + 一键升级"。
//   * 下载后**必须校验 SHA256** 才安装，且安装前剥掉下载文件的"网络来源标记"(MOTW)，
//     否则会二次弹 SmartScreen 警告。
//   * 便携版不走 MSI 安装（否则会装出第二份），只提示去下载页。
//   * 诚实说明：哈希来自同一分发渠道（HTTPS + GitHub），能防传输损坏与镜像篡改，
//     但不等于代码签名；签名后应改成校验签名。
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace ui {

struct UpdateInfo {
    QString version;      // 1.0.1
    QString tag;          // v1.0.1
    QString notesUrl;     // release 页
    QUrl msiUrl;
    QString msiSha256;
    qint64 msiSize = 0;
    QUrl portableUrl;
    QString portableSha256;
    bool valid() const { return !version.isEmpty() && msiUrl.isValid() && !msiSha256.isEmpty(); }
};

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // ---- 偏好（存 QSettings，键名与设置页共用）----
    static bool autoCheckEnabled();
    static void setAutoCheckEnabled(bool on);
    static bool autoCheckDue(int intervalHours = 24);  // 距上次检查是否已超过间隔
    static void markChecked();
    static QString skippedVersion();
    static void skipVersion(const QString& version);

    // 运行的是安装版（%LOCALAPPDATA%\MiderHive，兼容旧品牌 %LOCALAPPDATA%\AgentHive）还是便携版
    static bool isInstalledCopy();

    void check();                                       // 拉清单并比对版本
    void downloadAndInstall(const UpdateInfo& info);    // 下载 → 校验 → 安装 → 退出

signals:
    void checking();
    void upToDate(const QString& currentVersion);
    void updateAvailable(const ui::UpdateInfo& info);
    void failed(const QString& reason);
    void downloadProgress(qint64 received, qint64 total);
    void verifying();
    void installing();   // 已启动安装程序，调用方应准备退出

private:
    void downloadMsi(const UpdateInfo& info);
    void verifyAndInstall(const UpdateInfo& info, const QString& filePath);

    QNetworkAccessManager* net_ = nullptr;
};

}  // namespace ui
