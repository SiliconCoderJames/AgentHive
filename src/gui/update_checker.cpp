#include "update_checker.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include "core/types.h"   // ah::kPlatformVersion
#include "core/util.h"
#include "core/version_util.h"
#include "i18n.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace ui {

namespace {

constexpr const char* kRepoSlug = "SiliconCoderJames/AgentHive";
constexpr const char* kReleasesPage = "https://github.com/SiliconCoderJames/AgentHive/releases";

// 清单地址：默认走 releases/latest 的固定资产 URL；可用环境变量覆盖（便于本地联调与 fork）
QUrl manifestUrl() {
    const std::string override = ah::envOr("AGENTHIVE_UPDATE_URL", "ZCODE_UPDATE_URL");
    if (!override.empty()) return QUrl(QString::fromStdString(override));
    return QUrl(QString("https://github.com/%1/releases/latest/download/latest.json")
                    .arg(QString::fromLatin1(kRepoSlug)));
}

QNetworkRequest makeRequest(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QString("AgentHive/%1").arg(QString::fromLatin1(ah::kPlatformVersion)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);  // release 资产会 302 到 CDN
    req.setTransferTimeout(15000);
    return req;
}

QString tempMsiPath(const QString& version) {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QString("AgentHive-%1-x64.msi").arg(version));
}

// 校验通过后剥掉"网络来源标记"：否则 msiexec 会因文件来自互联网再弹一次 SmartScreen
void stripMarkOfTheWeb(const QString& path) {
#ifdef _WIN32
    const QString stream = path + ":Zone.Identifier";
    DeleteFileW(reinterpret_cast<const wchar_t*>(stream.utf16()));
#else
    Q_UNUSED(path);
#endif
}

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {}

bool UpdateChecker::autoCheckEnabled() {
    QSettings s;
    return s.value("ui/autoCheck", true).toBool();
}
void UpdateChecker::setAutoCheckEnabled(bool on) {
    QSettings s;
    s.setValue("ui/autoCheck", on);
}

bool UpdateChecker::autoCheckDue(int intervalHours) {
    QSettings s;
    const QString last = s.value("ui/lastCheckUtc").toString();
    if (last.isEmpty()) return true;
    const QDateTime t = QDateTime::fromString(last, Qt::ISODate);
    if (!t.isValid()) return true;
    return t.secsTo(QDateTime::currentDateTimeUtc()) >= qint64(intervalHours) * 3600;
}
void UpdateChecker::markChecked() {
    QSettings s;
    s.setValue("ui/lastCheckUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
}

QString UpdateChecker::skippedVersion() {
    QSettings s;
    return s.value("ui/skippedVersion").toString();
}
void UpdateChecker::skipVersion(const QString& version) {
    QSettings s;
    s.setValue("ui/skippedVersion", version);
}

bool UpdateChecker::isInstalledCopy() {
    // 安装版固定落在 %LOCALAPPDATA%\AgentHive（GenericDataLocation 在 Windows 上即 %LOCALAPPDATA%），
    // 其余位置（解压出来的便携版、开发时的 build 目录）都按便携版处理
    const QString appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
    const QString localRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (localRoot.isEmpty()) return false;
    const QString expected = QDir::cleanPath(localRoot + "/AgentHive");
    return appDir.compare(expected, Qt::CaseInsensitive) == 0;
}

void UpdateChecker::check() {
    emit checking();
    trace(QString("check() start, url=%1").arg(manifestUrl().toString()));
    if (!net_) net_ = new QNetworkAccessManager(this);
    QNetworkReply* reply = net_->get(makeRequest(manifestUrl()));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            trace(QString("check() network error: %1").arg(reply->errorString()));
            emit failed(reply->errorString());
            return;
        }
        const QByteArray raw = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject()) {
            emit failed(i18n::trs("更新清单格式无法解析", "malformed update manifest"));
            return;
        }
        const QJsonObject o = doc.object();
        UpdateInfo info;
        info.version = o.value("version").toString();
        info.tag = o.value("tag").toString();
        info.notesUrl = o.value("notes_url").toString(kReleasesPage);
        const QJsonObject msi = o.value("msi").toObject();
        info.msiUrl = QUrl(msi.value("url").toString());
        info.msiSha256 = msi.value("sha256").toString().toLower();
        info.msiSize = static_cast<qint64>(msi.value("size").toDouble());
        const QJsonObject portable = o.value("portable").toObject();
        info.portableUrl = QUrl(portable.value("url").toString());
        info.portableSha256 = portable.value("sha256").toString().toLower();

        markChecked();
        const QString current = QString::fromLatin1(ah::kPlatformVersion);
        trace(QString("manifest bytes=%1 version='%2' msi='%3' sha=%4chars current='%5' valid=%6 newer=%7")
                  .arg(raw.size())
                  .arg(info.version, info.msiUrl.toString())
                  .arg(info.msiSha256.size())
                  .arg(current)
                  .arg(info.valid() ? 1 : 0)
                  .arg(ah::isNewerVersion(info.version.toStdString(), current.toStdString()) ? 1 : 0));
        if (!info.valid()) {
            emit failed(i18n::trs("更新清单缺少必要字段", "manifest is missing required fields"));
            return;
        }
        if (ah::isNewerVersion(info.version.toStdString(), current.toStdString()))
            emit updateAvailable(info);
        else
            emit upToDate(current);
    });
}

void UpdateChecker::downloadAndInstall(const UpdateInfo& info) {
    if (!isInstalledCopy()) {
        // 便携版：装 MSI 会在系统里多出一份，交给用户自己选
        emit failed(i18n::trs("便携版请到下载页手动更新（避免装出第二份）",
                              "portable build: download manually to avoid a second copy"));
        return;
    }
    if (!info.msiUrl.isValid() || info.msiSha256.isEmpty()) {
        emit failed(i18n::trs("清单里没有可用的安装包信息", "manifest has no usable installer entry"));
        return;
    }
    downloadMsi(info);
}

void UpdateChecker::downloadMsi(const UpdateInfo& info) {
    if (!net_) net_ = new QNetworkAccessManager(this);
    const QString path = tempMsiPath(info.version);
    QFile::remove(path);  // 覆盖上次残留的下载

    QNetworkReply* reply = net_->get(makeRequest(info.msiUrl));
    auto* out = new QFile(path, this);
    if (!out->open(QIODevice::WriteOnly)) {
        delete out;
        emit failed(i18n::trs("无法写入临时目录", "cannot write to the temp directory"));
        return;
    }
    connect(reply, &QNetworkReply::readyRead, this, [reply, out] { out->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 got, qint64 total) { emit downloadProgress(got, total); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, out, info, path] {
        out->write(reply->readAll());
        out->close();
        out->deleteLater();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(path);
            emit failed(reply->errorString());
            return;
        }
        verifyAndInstall(info, path);
    });
}

void UpdateChecker::verifyAndInstall(const UpdateInfo& info, const QString& filePath) {
    emit verifying();
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit failed(i18n::trs("下载的文件无法读取", "downloaded file cannot be read"));
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        emit failed(i18n::trs("计算校验和失败", "failed to hash the download"));
        return;
    }
    f.close();
    const QString actual = QString::fromLatin1(hash.result().toHex());
    if (actual.compare(info.msiSha256, Qt::CaseInsensitive) != 0) {
        QFile::remove(filePath);
        emit failed(i18n::trs("校验和不匹配，已删除下载文件（可能被篡改或传输损坏）",
                              "checksum mismatch - download deleted (tampered or corrupted)"));
        return;
    }
    stripMarkOfTheWeb(filePath);
    // /passive：显示进度但不打断用户；per-user 安装不需要管理员权限
    const bool started = QProcess::startDetached("msiexec",
                                                 {"/i", QDir::toNativeSeparators(filePath),
                                                  "/passive", "/norestart"});
    if (!started) {
        trace("verifyAndInstall: msiexec launch failed");
        emit failed(i18n::trs("无法启动安装程序", "failed to launch the installer"));
        return;
    }
    trace(QString("verifyAndInstall: hash ok, msiexec started for %1").arg(filePath));
    emit installing();
}

}  // namespace ui
