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
#include <QTimer>
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
    auto *reply = network.get(request(QUrl("https://github.com/RePiplup/Eego/releases/latest/download/eego-update.json")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const bool notify = reportResult;
        reportResult = false;
        busy = false;
        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value("tag").toString();
        static const QRegularExpression stable("^v([0-9]+\\.[0-9]+\\.[0-9]+)$");
        const auto match = stable.match(tag);
        if (reply->error() || !match.hasMatch()) {
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
        static const QRegularExpression sha("^[0-9a-f]{64}$");
        const auto digest = release.value("sha256").toString();
        const qint64 size = release.value("size").toInteger();
        if (release.value("name").toString() == name && sha.match(digest).hasMatch() && size > 0 && size <= 2147483648LL) {
            busy = true;
            QMessageBox prompt(QMessageBox::Information, tr("发现新版本"),
                tr("Eego %1 已发布。下载完成后将保存任务、退出并覆盖安装。").arg(version), QMessageBox::NoButton);
            auto *install = prompt.addButton(tr("下载并安装"), QMessageBox::AcceptRole);
            prompt.addButton(tr("稍后"), QMessageBox::RejectRole);
            prompt.exec();
            if (prompt.clickedButton() == install)
                download(QUrl(url), name, digest.toLatin1(), size);
            else busy = false;
            return;
        }
        if (notify) QMessageBox::warning(nullptr, tr("检查更新"), tr("新版尚无可验证的 Windows 安装包，请稍后重试。"));
    });
}

struct Updater::Transfer {
    QUrl origin;
    QUrl source;
    std::shared_ptr<QSaveFile> file;
    QByteArray digest;
    qint64 size;
    QProgressDialog *progress;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    int retries = 0;
};

void Updater::download(const QUrl &url, const QString &name, QByteArray expectedHash, qint64 size) {
    const QDir cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates");
    auto file = std::make_shared<QSaveFile>(cache.filePath(name));
    if (!cache.mkpath(".") || !file->open(QIODevice::WriteOnly)) {
        busy = false;
        QMessageBox::warning(nullptr, tr("更新失败"), tr("无法写入更新缓存。"));
        return;
    }
    auto *progress = new QProgressDialog(tr("正在下载安装包…"), tr("取消"), 0, 100);
    progress->setWindowTitle(tr("更新 Eego"));
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->show();
    auto transfer = std::shared_ptr<Transfer>(new Transfer{url, url, file, expectedHash, size, progress});
    fetchPart(transfer);
}

void Updater::fetchPart(const std::shared_ptr<Transfer> &transfer) {
    const qint64 start = transfer->file->pos();
    const qint64 end = qMin(start + 4 * 1024 * 1024 - 1, transfer->size - 1);
    const auto range = QByteArray::number(start) + "-" + QByteArray::number(end);
    auto r = request(transfer->source);
    r.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    r.setRawHeader("Range", "bytes=" + range);
    r.setRawHeader("Accept-Encoding", "identity");
    auto *reply = network.get(r);
    connect(transfer->progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    // Accumulate one bounded part. A failed part can be requested again without
    // writing duplicate bytes or invalidating the hash of previous parts.
    auto part = std::make_shared<QByteArray>();
    const auto consume = [reply, part, start, end] {
        part->append(reply->readAll());
        if (part->size() > end - start + 1 && !reply->isFinished()) reply->abort();
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    QTimer::singleShot(90000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [=] {
        reply->deleteLater();
        auto *progress = transfer->progress;
        if (progress->wasCanceled()) {
            progress->deleteLater();
            busy = false;
            return;
        }
        consume();
        const QByteArray &bytes = *part;
        const QByteArray contentRange = "bytes " + range + "/" + QByteArray::number(transfer->size);
        const bool wholeFile = start == 0 && end + 1 == transfer->size;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool validRange = status == 206 && reply->rawHeader("Content-Range") == contentRange;
        if (reply->error() || bytes.size() != end - start + 1 || !(validRange || (wholeFile && status == 200))) {
            if (++transfer->retries <= 3) {
                transfer->source = transfer->origin;
                QTimer::singleShot(500 * transfer->retries, this, [this, transfer] {
                    if (!transfer->progress->wasCanceled()) fetchPart(transfer);
                    else { transfer->progress->deleteLater(); busy = false; }
                });
                return;
            }
            busy = false;
            progress->deleteLater();
            QMessageBox::warning(nullptr, tr("更新失败"), tr("安装包下载中断，请检查网络后重试。"));
            return;
        }
        if (transfer->file->write(bytes) == bytes.size()) {
            transfer->hash.addData(bytes);
            progress->setValue(int(transfer->file->pos() * 100 / transfer->size));
            if (transfer->file->pos() < transfer->size) {
                transfer->source = reply->url();
                transfer->retries = 0;
                fetchPart(transfer);
                return;
            }
            if (transfer->hash.result().toHex() == transfer->digest && transfer->file->commit()) {
                progress->deleteLater();
                installer = transfer->file->fileName();
                emit installationReady();
                return;
            }
        }
        busy = false;
        progress->deleteLater();
        QMessageBox::warning(nullptr, tr("更新失败"), tr("文件写入或完整性校验失败。可在设置中重试。"));
    });
}

bool Updater::launchInstaller() const {
    if (installer.isEmpty()) return true;
    return QProcess::startDetached(installer, {"/SILENT", "/NORESTART", "/EEGOUPDATE=1",
        "/DIR=" + QDir::toNativeSeparators(QCoreApplication::applicationDirPath())});
}
