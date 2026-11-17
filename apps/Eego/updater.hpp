#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class Updater final : public QObject {
    Q_OBJECT
public:
    explicit Updater(QNetworkAccessManager &network) : network(network) {}
    Q_INVOKABLE void check(bool manual);
    bool launchInstaller() const;
signals:
    void installationReady();
private:
    void download(const QUrl &url, const QString &name, QByteArray expectedHash, qint64 size);
    QNetworkAccessManager &network;
    bool busy = false;
    bool reportResult = false;
    QString installer;
};
