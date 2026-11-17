#include "updater.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVersionNumber>
#include <memory>

namespace {
QNetworkRequest request(const QUrl &url) {
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, "Eego/" + QCoreApplication::applicationVersion());
    r.setTransferTimeout(30000);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return r;
}
}

void Updater::check(bool manual) {
    if (manual) reportResult = true;
    if (busy) return;
    busy = true;
    auto *reply = network.get(request(QUrl("https://api.github.com/repos/RePiplup/Eego/releases/latest")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const bool notify = reportResult;
        reportResult = false;
        busy = false;
        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value("tag_name").toString();
        static const QRegularExpression stable("^v([0-9]+\\.[0-9]+\\.[0-9]+)$");
        const auto match = stable.match(tag);
        if (reply->error() || !match.hasMatch() || release.value("draft").toBool(true) ||
            release.value("prerelease").toBool(true)) {
            if (notify) QMessageBox::information(nullptr, tr("检查更新"), tr("无法读取正式版本信息，请检查网络后重试。"));
            return;
        }
        const QString version = match.captured(1);
        if (QVersionNumber::fromString(version) <= QVersionNumber::fromString(QCoreApplication::applicationVersion())) {
            if (notify) QMessageBox::information(nullptr, tr("检查更新"), tr("当前已是最新版本。"));
            return;
        }
        const QString name = "Eego-" + version + "-windows-x64-setup.exe";
        const QString url = "https://github.com/RePiplup/Eego/releases/download/" + tag + "/" + name;
        for (const auto assetValue : release.value("assets").toArray()) {
            const auto asset = assetValue.toObject();
            const QString digest = asset.value("digest").toString();
            static const QRegularExpression sha("^sha256:([0-9a-f]{64})$");
            const auto hash = sha.match(digest);
            const qint64 size = asset.value("size").toInteger();
            if (asset.value("name").toString() != name || asset.value("browser_download_url").toString() != url ||
                !hash.hasMatch() || size <= 0 || size > 2147483648LL) continue;
            busy = true;
            QMessageBox prompt(QMessageBox::Information, tr("发现新版本"),
                tr("Eego %1 已发布。下载完成后将保存任务、退出并覆盖安装。").arg(version), QMessageBox::NoButton);
            auto *install = prompt.addButton(tr("下载并安装"), QMessageBox::AcceptRole);
            prompt.addButton(tr("稍后"), QMessageBox::RejectRole);
            prompt.exec();
            if (prompt.clickedButton() == install)
                download(QUrl(url), name, hash.captured(1).toLatin1(), size);
            else busy = false;
            return;
        }
        if (notify) QMessageBox::warning(nullptr, tr("检查更新"), tr("新版尚无可验证的 Windows 安装包，请稍后重试。"));
    });
}

void Updater::download(const QUrl &url, const QString &name, QByteArray expectedHash, qint64 size) {
    const QDir cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates");
    auto file = std::make_shared<QSaveFile>(cache.filePath(name));
    if (!cache.mkpath(".") || !file->open(QIODevice::WriteOnly)) {
        busy = false;
        QMessageBox::warning(nullptr, tr("更新失败"), tr("无法写入更新缓存。"));
        return;
    }
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto *progress = new QProgressDialog(tr("正在下载安装包…"), tr("取消"), 0, 100);
    progress->setWindowTitle(tr("更新 Eego"));
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->show();
    auto *reply = network.get(request(url));
    reply->setReadBufferSize(1024 * 1024);
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    auto consume = [=] {
        const QByteArray data = reply->readAll();
        if (data.isEmpty()) return;
        if (file->pos() + data.size() > size || file->write(data) != data.size()) {
            reply->abort();
            return;
        }
        hash->addData(data);
        progress->setValue(int(file->pos() * 100 / size));
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    connect(reply, &QNetworkReply::finished, this, [=] {
        reply->deleteLater();
        progress->deleteLater();
        if (!reply->error()) consume();
        const bool canceled = progress->wasCanceled();
        if (canceled || reply->error() || file->pos() != size || hash->result().toHex() != expectedHash || !file->commit()) {
            busy = false;
            if (!canceled) QMessageBox::warning(nullptr, tr("更新失败"), tr("下载或完整性校验失败。可在设置中重试。"));
            return;
        }
        installer = file->fileName();
        emit installationReady();
    });
}

bool Updater::launchInstaller() const {
    if (installer.isEmpty()) return true;
    return QProcess::startDetached(installer, {"/SILENT", "/NORESTART", "/EEGOUPDATE=1",
        "/DIR=" + QDir::toNativeSeparators(QCoreApplication::applicationDirPath())});
}
