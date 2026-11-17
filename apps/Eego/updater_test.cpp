// Exercise the release/asset requests and user prompt without launching an installer.
#include "updater.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QRegularExpression>
#include <QTimer>

class FixtureNetwork final : public QNetworkAccessManager {
public:
    QTemporaryDir files;
    const QString assetUrl = "https://github.com/RePiplup/Eego/releases/download/v0.10.2/Eego-0.10.2-windows-x64-setup.exe";
    QStringList requests;
    QTcpServer server;
    bool interruptOnce = false;
    int rangeRequests = 0;
    FixtureNetwork() {
        if (!server.listen(QHostAddress::LocalHost)) qFatal("Cannot start fixture server");
        connect(&server, &QTcpServer::newConnection, this, [this] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this,socket] {
                if (!socket->peek(8192).contains("\r\n\r\n")) return;
                const auto headers = QString::fromLatin1(socket->readAll());
                static const QRegularExpression range("Range: bytes=(\\d+)-(\\d+)", QRegularExpression::CaseInsensitiveOption);
                const auto match = range.match(headers);
                if (!match.hasMatch()) qFatal("Missing range");
                ++rangeRequests;
                if (interruptOnce && rangeRequests == 2) {
                    socket->write("HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                } else {
                    QFile file(files.filePath("asset"));
                    if (!file.open(QIODevice::ReadOnly) || !file.seek(match.captured(1).toLongLong())) qFatal("Invalid fixture");
                    const auto bytes = file.read(match.captured(2).toLongLong() - file.pos() + 1);
                    socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " +
                        match.captured(1).toLatin1() + "-" + match.captured(2).toLatin1() + "/" + QByteArray::number(file.size()) +
                        "\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\nConnection: close\r\n\r\n" + bytes);
                }
                socket->disconnectFromHost();
            });
        });
    }
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override {
        const auto url = request.url().toString();
        requests.append(url);
        QString file = "missing";
        if (url == "https://github.com/RePiplup/Eego/releases/latest/download/eego-update.json") file = "metadata";
        if (url == assetUrl || request.url().host() == "127.0.0.1") {
            QNetworkRequest local(request);
            local.setUrl(QUrl(QString("http://127.0.0.1:%1/asset").arg(server.serverPort())));
            return QNetworkAccessManager::createRequest(op, local, data);
        }
        return QNetworkAccessManager::createRequest(op, QNetworkRequest(QUrl::fromLocalFile(files.filePath(file))), data);
    }
};

int main(int argc, char **argv) {
    QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("Eego-updater-test");
    QStandardPaths::setTestModeEnabled(true);
    const QString scenario = app.arguments().value(1);
    app.setApplicationVersion(scenario == "current" ? "0.10.2" : "0.10.1");
    FixtureNetwork network;
    network.interruptOnce = scenario == "retry";
    const QByteArray bytes = network.interruptOnce ? QByteArray(4*1024*1024+57,'z') : QByteArray("fixture installer bytes");
    const QByteArray digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    const QJsonObject release{{"tag","v0.10.2"},{"name","Eego-0.10.2-windows-x64-setup.exe"},
                              {"size",bytes.size()},{"sha256",QString::fromLatin1(digest)}};
    QFile metadata(network.files.filePath("metadata")), payload(network.files.filePath("asset"));
    if (!metadata.open(QIODevice::WriteOnly) || !payload.open(QIODevice::WriteOnly)) return 2;
    metadata.write(QJsonDocument(release).toJson());
    payload.write(scenario == "corrupt" ? QByteArray(bytes.size(),'x') : bytes);
    metadata.close();
    payload.close();
    Updater updater(network);
    bool accepted = false;
    QObject::connect(&updater,&Updater::installationReady,&app,[&] {
        const bool expected = (scenario == "accept" && network.requests.size() == 2)
                           || (scenario == "retry" && network.rangeRequests == 3);
        app.exit(expected && accepted ? 0 : 3);
    });
    QTimer dialogs;
    QObject::connect(&dialogs,&QTimer::timeout,&app,[&] {
        auto *box = qobject_cast<QMessageBox*>(app.activeModalWidget());
        if (!box) return;
        if (box->windowTitle() == "发现新版本") {
            const bool defer = scenario == "defer";
            for (auto *button : box->buttons()) {
                if (button->text() != (defer ? "稍后" : "下载并安装")) continue;
                accepted = !defer;
                button->click();
                if (defer) QTimer::singleShot(40,&app,[&]{ app.exit(network.requests.size() == 1 ? 0 : 4); });
                break;
            }
        } else {
            const bool valid = (scenario == "current" && box->text().contains("已是最新") && network.requests.size() == 1)
                            || (scenario == "corrupt" && box->windowTitle() == "更新失败" && network.requests.size() == 2);
            box->accept();
            QTimer::singleShot(0,&app,[&,valid]{app.exit(valid ? 0 : 5);});
        }
    });
    dialogs.start(10);
    QTimer::singleShot(10000,&app,[&]{app.exit(6);});
    QTimer::singleShot(0,&updater,[&]{updater.check(true);});
    return app.exec();
}
