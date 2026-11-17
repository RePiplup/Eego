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
#include <QTimer>

class FixtureNetwork final : public QNetworkAccessManager {
public:
    QTemporaryDir files;
    const QString assetUrl = "https://github.com/RePiplup/Eego/releases/download/v0.10.2/Eego-0.10.2-windows-x64-setup.exe";
    QStringList requests;
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override {
        const auto url = request.url().toString();
        requests.append(url);
        QString file = "missing";
        if (url == "https://api.github.com/repos/RePiplup/Eego/releases/latest") file = "metadata";
        if (url == assetUrl) file = "asset";
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
    const QByteArray bytes("fixture installer bytes");
    const QByteArray digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    const QJsonObject asset{{"name","Eego-0.10.2-windows-x64-setup.exe"},{"browser_download_url",network.assetUrl},
                            {"size",bytes.size()},{"digest","sha256:" + QString::fromLatin1(digest)}};
    const QJsonObject release{{"tag_name","v0.10.2"},{"draft",false},{"prerelease",false},{"assets",QJsonArray{asset}}};
    QFile metadata(network.files.filePath("metadata")), payload(network.files.filePath("asset"));
    if (!metadata.open(QIODevice::WriteOnly) || !payload.open(QIODevice::WriteOnly)) return 2;
    metadata.write(QJsonDocument(release).toJson());
    payload.write(scenario == "corrupt" ? QByteArray(bytes.size(),'x') : bytes);
    metadata.close();
    payload.close();
    Updater updater(network);
    bool accepted = false;
    QObject::connect(&updater,&Updater::installationReady,&app,[&] {
        app.exit(scenario == "accept" && accepted && network.requests.size() == 2 ? 0 : 3);
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
