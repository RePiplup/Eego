// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>
#include <QTranslator>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <iostream>
#include "library_continuity.hpp"
#include "core/qt_library.hpp"
#include <QFileDialog>
#include <QSettings>
#include <QTemporaryFile>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWindow>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QEventLoop>
#include "core/browser_bridge.hpp"
#include <functional>
#include <sstream>
#include <iomanip>
static std::function<LRESULT(COPYDATASTRUCT*)> receiver;
static std::function<void()> restoreWindow;
static const UINT showWindowMessage=RegisterWindowMessageW(L"Eego.ShowMainWindow");
static LRESULT CALLBACK bridgeProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==showWindowMessage&&restoreWindow){restoreWindow();return 1;}
    if(m==WM_COPYDATA && receiver)return receiver(reinterpret_cast<COPYDATASTRUCT*>(l));
    return DefWindowProcW(h,m,w,l);
}

class DownloadService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int maxActiveTasks READ maxActiveTasks NOTIFY concurrencyChanged)
    Q_PROPERTY(bool settingsPending READ settingsPending NOTIFY concurrencyChanged)
    Q_PROPERTY(QString settingsWarning READ settingsWarning NOTIFY concurrencyChanged)
    Q_PROPERTY(int busyId READ busyId NOTIFY busyChanged)
    Q_PROPERTY(QString downloadPath READ downloadPath NOTIFY downloadPathChanged)
    Q_PROPERTY(QVariantList startupTasks READ startupTasks CONSTANT)
public:
#ifdef EEGO_SERVICE_TEST
    void testHost(const QString &program,const QStringList &arguments){host.setProgram(program);host.setArguments(arguments);}
#endif
    explicit DownloadService(const QString &library) : savePath(QDir::fromNativeSeparators(QString::fromStdWString(qt_library::downloads(library.toStdWString()).wstring()))) {
        settings.reset(new QSettings(QDir(library).filePath("preferences.ini"), QSettings::IniFormat));
        if(!settings->contains("downloadPath"))QDir().mkpath(savePath);
        savePath=settings->value("downloadPath",savePath).toString();
        connect(&host, &QProcess::stateChanged, this, [this] { emit availableChanged(); });
        host.setProgram(QCoreApplication::applicationDirPath() + "/core/task_service_host_r3.exe");
        host.setArguments({"--data-dir", library});
        connect(&host, &QProcess::readyReadStandardOutput, this, [this] {
            while (host.canReadLine()) {
                const auto response = QJsonDocument::fromJson(host.readLine()).object();
                const QString id = response.value("requestId").toString();
                if(!pendingExternal.isEmpty() && id==pendingExternal){externalResponse=response;pendingExternal.clear();emit externalDone();}
                if (id == "list") listing = false;
                if(id=="settings") { concurrencyPending=false;emit concurrencyChanged(); }
                if(response.value("ok").toBool() && response.contains("maxActiveTasks")) {
                    auto next=response.value("maxActiveTasks").toInt(1);
                    auto message=response.value("settingsError").toString();
                    if(next!=concurrency||message!=warning){concurrency=next;warning=message;emit concurrencyChanged();}
                }
                if (id == "action") {
                    // Queue a fresh snapshot after the host has applied the action.
                    send({{"op","list"},{"requestId","afterAction"}});
                }
                if (id == "afterAction") { actionPending = false; pendingId = -1; emit busyChanged(); }
                if (!response.value("ok").toBool()) {
                    if (id == "action") { actionPending = false; pendingId = -1; emit busyChanged(); }
                    emit problem(response.value("error").toObject().value("message").toString());
                } else if (id == "list" || id == "afterAction") {
                    lastTasks=response.value("tasks").toArray();emit updated(lastTasks.toVariantList());
                } else if (id == "create") {
                    emit created(response.value("id").toInt());
                }
            }
        });
        connect(&host, &QProcess::readyReadStandardError, this, [this] {
            diagnostics = (diagnostics + host.readAllStandardError()).right(2048);
        });
        connect(&host, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            emit problem(tr("下载服务无法运行。请检查 core 目录内的程序及 DLL。%1").arg(host.errorString()));
            if (e == QProcess::FailedToStart && closing) QCoreApplication::exit(0);
        });
        connect(&host, &QProcess::finished, this, [this] {
            poll.stop();
            if (closing) QCoreApplication::exit(0);
            else emit problem(tr("下载服务已退出。%1").arg(QString::fromUtf8(diagnostics)));
        });
        connect(&poll, &QTimer::timeout, this, [this] {
            if (!closing && !listing && host.state() == QProcess::Running)
                listing = send({{"op","list"},{"requestId","list"}});
        });
    }
    int maxActiveTasks() const {return concurrency;}
    bool settingsPending() const {return concurrencyPending;}
    QString settingsWarning() const {return warning;}
    Q_INVOKABLE void setMaxActiveTasks(int value) {
        if(closing||concurrencyPending)return;
        concurrencyPending=send({{"op","settings"},{"requestId","settings"},{"maxActiveTasks",value}});
        emit concurrencyChanged();
    }
    int busyId() const { return pendingId; }
    QVariantList startupTasks() const {
        // Read only the small task index. The service verifies downloaded files separately.
        QFile file(QFileInfo(settings->fileName()).dir().filePath("tasks.record"));
        if(!file.open(QIODevice::ReadOnly)||file.size()>8*1024*1024)return {};
        const auto record=file.readAll();
        const QByteArray prefix="HBT-CHECKPOINT-1\n";
        if(!record.startsWith(prefix)||record.size()<prefix.size()+73)return {};
        const auto payload=record.mid(prefix.size(),record.size()-prefix.size()-73);
        if(record.right(73)!="\nSHA256 "+QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()+"\n")return {};
        std::istringstream in(payload.toStdString());
        std::string magic,url,path,expected,digest,error;
        int next,count;
        if(!(in>>magic>>next>>count)||(magic!="GUI1"&&magic!="GUI2")||count<0||count>500)return {};
        const auto names=settings->value("taskDisplayNames").toMap();
        QVariantList result;
        for(int i=0;i<count;++i){
            int id,state;quint64 bytes,total;
            if(!(in>>id>>state>>bytes>>total>>std::quoted(url)>>std::quoted(path)>>std::quoted(expected)>>std::quoted(digest)>>std::quoted(error)))return {};
            const auto target=QString::fromStdString(path);
            const auto name=names.value(QString::number(id),QFileInfo(target).fileName()).toString();
            result.append(QVariantMap{{"id",id},{"name",name},{"path",target},{"url",QString::fromStdString(url)},
                {"status","syncing"},{"bytes",bytes},{"total",total?QVariant(total):QVariant::fromValue(nullptr)},
                {"speed",0},{"files",QVariantList{}},{"capabilities",QVariantMap{}}});
        }
        return result;
    }
    QString downloadPath() const { return savePath; }
    Q_INVOKABLE bool trayAvailable() const { return QSystemTrayIcon::isSystemTrayAvailable(); }
    bool available() const { return host.state() == QProcess::Running; }
    void start() {
        connect(&host,&QProcess::started,this,[this]{listing=send({{"op","list"},{"requestId","list"}});poll.start(350);});
        host.start();
        host.waitForStarted(5000);
    }
    Q_INVOKABLE QString chooseDirectory() {
        return QFileDialog::getExistingDirectory(nullptr, tr("选择下载目录"), savePath);
    }
    bool validDirectory(const QString &path) {
        if (!QDir::isAbsolutePath(path) || !QDir(path).exists()) { emit problem(tr("请选择已存在的绝对目录")); return false; }
        QTemporaryFile probe(QDir(path).filePath(".eego-write-XXXXXX"));
        if (!probe.open()) { emit problem(tr("目录不可写")); return false; }
        return true;
    }
    Q_INVOKABLE void setDefaultDirectory(const QString &path) {
        if(path.isEmpty() || !validDirectory(path))return;
        settings->setValue("downloadPath",path);settings->sync();
        if(settings->status()!=QSettings::NoError){emit problem(tr("保存设置失败"));return;}
        savePath=path;emit downloadPathChanged();
    }
    int external(const QString &input, bool torrent, const QString &filename = {}) {
        if(!pendingExternal.isEmpty()||closing||!validDirectory(savePath))return 0;
        if(!torrent){
            auto existing=transact({{"op","list"}});
            if(!existing.value("ok").toBool())return 0;
            for(const auto &v:existing.value("tasks").toArray())if(v.toObject().value("url").toString()==input)return v.toObject().value("id").toInt();
        }
        auto response=transact({{"op",torrent?"torrent":"create"},{torrent?"file":"url",input},{"destination",savePath},{"filename",filename}});
        return response.value("ok").toBool()?response.value("id").toInt():0;
    }
    QJsonObject transact(QJsonObject request) {
        if(!pendingExternal.isEmpty()||closing)return {};
        pendingExternal="external-"+QString::number(++externalSerial);externalResponse={};
        request.insert("requestId",pendingExternal);
        QEventLoop loop;QTimer deadline;deadline.setSingleShot(true);
        connect(this,&DownloadService::externalDone,&loop,&QEventLoop::quit);
        connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        if(!send(request)){pendingExternal.clear();return {};}
        deadline.start(3500);loop.exec();
        // A timeout remains pending until its exact response arrives: fail closed,
        // never let a late create response acknowledge a different request.
        return externalResponse;
    }
    int externalStatus(int id){
        auto response=transact({{"op","list"}});if(!response.value("ok").toBool())return 0;
        for(auto v:response.value("tasks").toArray()){
            auto t=v.toObject();if(t.value("id").toInt()!=id)continue;
            auto state=t.value("status").toString();
            if(state=="completed")return QFileInfo(t.value("path").toString()).isFile()?2:0;
            if(state=="failed")return t.value("error").toObject().value("code").toString()=="BROWSER_AUTHORIZATION_REQUIRED"?5:3;
            if(state=="paused")return 4;
            return state=="downloading"||state=="queued"?1:0;
        }return 0;
    }
    Q_INVOKABLE void openTorrent(QString destination = {}) {
        auto file=QFileDialog::getOpenFileName(nullptr,QObject::tr("打开种子"),{},tr("Torrent (*.torrent)"));
        if(!file.isEmpty())importTorrent(file,destination);
    }
    void importTorrent(const QString &file, QString destination = {}) {
        if(destination.isEmpty())destination=savePath;
        if(!validDirectory(destination))return;
        send({{"op","torrent"},{"requestId","create"},{"file",file},{"destination",destination}});
    }
    Q_INVOKABLE void selectFiles(int id, const QVariantList &indexes) {
        if(indexes.isEmpty()){emit problem(tr("至少选择一个文件；取消不会开始下载"));return;}
        QJsonArray a;for(const auto &v:indexes)a.append(v.toInt());
        send({{"op","select"},{"requestId","action"},{"id",id},{"indexes",a}});
    }
    Q_INVOKABLE void create(QString links, QString destination = {}) {
        if(destination.isEmpty())destination=savePath;
        if(!validDirectory(destination))return;
        links = links.trimmed();
        if (links.isEmpty() || links.toUtf8().size() > 8192) {
            emit problem(tr("请输入链接，每批最多 8192 个 UTF-8 字节。"));
            return;
        }
        for (const auto &entry : links.split('\n', Qt::SkipEmptyParts)) {
            const QString line = entry.trimmed();
            if (!line.isEmpty() && !line.startsWith("http://") && !line.startsWith("https://") && !line.startsWith("magnet:?")) {
                emit problem(tr("此样机支持 HTTP 和 HTTPS 链接。"));
                return;
            }
        }
        send({{"op","create"},{"requestId","create"},{"url",links},{"destination",destination}});
    }
    Q_INVOKABLE void action(const QString &op, int id) {
        if (!actionPending && !closing && (op == "pause" || op == "resume" || op == "retry")) {
            actionPending = send({{"op",op},{"requestId","action"},{"id",id}});
            pendingId = actionPending ? id : -1;
            emit busyChanged();
        }
    }
    Q_INVOKABLE void batch(const QString &op, const QVariantList &ids, bool deleteFiles=false) {
        if(actionPending||closing||ids.isEmpty())return;
        QJsonArray selected;for(const auto &id:ids)selected.append(id.toInt());
        actionPending=send({{"op","batch"},{"requestId","action"},{"action",op},{"ids",selected},{"deleteFiles",deleteFiles}});
        pendingId=actionPending?ids.first().toInt():-1;emit busyChanged();
    }
    Q_INVOKABLE void copyLinks(const QVariantList &ids) {
        QStringList links;for(const auto &v:lastTasks){auto t=v.toObject();if(ids.contains(t.value("id").toInt()))links.append(t.value("url").toString());}
        if(!links.isEmpty())QApplication::clipboard()->setText(links.join('\n'));
    }
    Q_INVOKABLE void openPath(int id, bool folder, int fileIndex=-1) {
        for(const auto &v:lastTasks){auto t=v.toObject();if(t.value("id").toInt()!=id)continue;
            QString path=t.value("path").toString();bool magnet=t.value("url").toString().startsWith("magnet:?");
            if(!folder&&t.value("status").toString()!="completed"){emit problem(tr("只能打开已完成的文件"));return;}
            if(magnet){
                if(folder&&(fileIndex<0||t.value("status").toString()!="completed")){if(QFileInfo(path).isFile())path=QFileInfo(path).absolutePath();}
                else {QString file;for(auto f:t.value("files").toArray()){auto o=f.toObject();if(o.value("index").toInt()==fileIndex&&o.value("selected").toBool())file=o.value("diskPath").toString();}
                    if(file.isEmpty()){emit problem(tr("请选择已下载的文件"));return;}
                    auto base=QFileInfo(path).canonicalFilePath();auto resolved=QFileInfo(file).canonicalFilePath();
                    if(base.isEmpty()||resolved.isEmpty()||(resolved.compare(base,Qt::CaseInsensitive)!=0&&!resolved.startsWith(base+"/",Qt::CaseInsensitive))){emit problem(tr("文件路径不可用"));return;}path=folder?QFileInfo(resolved).absolutePath():resolved;
                }
            }else if(folder)path=QFileInfo(path).absolutePath();
            if(!QFileInfo::exists(path)){emit problem(tr("文件或文件夹已不存在：%1").arg(path));return;}
            if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))emit problem(tr("文件存在，但 Windows 无法打开（请检查默认打开方式或访问权限）：%1").arg(path));
            return;
        }
    }
    Q_INVOKABLE void shutdown() {
        if (closing) return;
        closing = true;
        if(!lastTasks.isEmpty()){
            QVariantMap names;
            for(const auto &value:lastTasks){const auto t=value.toObject();names.insert(QString::number(t["id"].toInt()),t["name"].toString());}
            settings->setValue("taskDisplayNames",names);
        }
        poll.stop();
        if (host.state() == QProcess::NotRunning) QCoreApplication::exit(0);
        else if (!send({{"op","shutdown"},{"requestId","shutdown"}})) host.closeWriteChannel();
    }
signals:
    void concurrencyChanged();
    void externalDone();
    void downloadPathChanged();
    void availableChanged();
    void busyChanged();
    void updated(QVariantList tasks);
    void created(int id);
    void problem(QString message);
private:
    bool send(const QJsonObject &request) {
        if (host.state() != QProcess::Running ||
            host.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n') < 0) {
            emit problem(tr("下载服务不可用。"));
            return false;
        }
        return true;
    }
    int concurrency=1;bool concurrencyPending=false;QString warning;
    std::unique_ptr<QSettings> settings;
    QJsonArray lastTasks;QString pendingExternal;quint64 externalSerial=0;QJsonObject externalResponse;
    QProcess host;
    QTimer poll;
    QByteArray diagnostics;
    QString savePath;
    int pendingId = -1;
    bool listing = false, closing = false, actionPending = false;
};

#ifndef EEGO_SERVICE_TEST
#include "updater.hpp"
#include <QNetworkProxyFactory>
class UiText final : public QTranslator {
    QJsonObject labels;
public:
    UiText(){
        QFile config(QCoreApplication::applicationDirPath()+"/ui-text.json");
        if(config.open(QIODevice::ReadOnly))
            labels=QJsonDocument::fromJson(config.readAll()).object();
    }
    bool isEmpty() const override {return labels.isEmpty();}
    QString translate(const char*,const char* source,const char*,int) const override {
        return labels.value(QString::fromUtf8(source)).toString();
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationVersion(EEGO_VERSION);
    UiText text;
    app.installTranslator(&text);
    app.setWindowIcon(QIcon(":/qt/qml/HandmadeBT/assets/eego.ico"));
    QQuickStyle::setStyle("Basic");
    QCommandLineParser options;
    options.addHelpOption();
    options.addOption({"open","Open a local torrent","file"});
    options.addPositionalArgument("torrent", "Local torrent opened by Windows", "[torrent]");
    options.addOption({"data-dir","Task library directory","directory",
                       QCoreApplication::applicationDirPath() + "/userdata"});
    options.addOption({"check-library","Read-only task-library readiness check"});
    options.process(app);
    const QString openInput=options.isSet("open")?options.value("open"):options.positionalArguments().value(0);
    QString library;bool linked=!options.isSet("data-dir")&&QFile::exists(QCoreApplication::applicationDirPath()+"/library-path.txt");
    try {library=options.isSet("data-dir")?QDir(options.value("data-dir")).absolutePath():QString::fromStdWString(qt_library::resolve(std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString())).wstring());}
    catch(std::exception const&e){if(!options.isSet("check-library"))QMessageBox::warning(nullptr,QObject::tr("任务库不可用"),QString::fromUtf8(e.what()));return 4;}
    if(options.isSet("check-library")) {
      continuity::Lock probe(library);std::cout<<QJsonDocument(QJsonObject{{"library",library},{"available",probe.acquired()},{"linked",linked},{"errorCode",int(probe.error)}}).toJson(QJsonDocument::Compact).constData()<<std::endl;
      return probe.acquired()?0:2;
    }
    auto cls=browser_bridge::window_class(std::filesystem::path(library.toStdWString()));
    struct StartupGate {HANDLE h;bool held=false;~StartupGate(){if(held)ReleaseMutex(h);if(h)CloseHandle(h);}} gate{CreateMutexW(nullptr,FALSE,(L"Local\\"+cls+L"-startup").c_str())};
    if(!gate.h)return 4;
    auto lockResult=WaitForSingleObject(gate.h,15000);gate.held=lockResult==WAIT_OBJECT_0||lockResult==WAIT_ABANDONED;
    if(!gate.held)return 4;
    if(auto existing=FindWindowW(cls.c_str(),nullptr)){
        if(linked&&!continuity::sameApplication(existing)){QMessageBox::information(nullptr,QObject::tr("请先关闭旧版"),QObject::tr("原任务库仍由旧版使用。请先在旧版窗口正常退出，再打开此统一入口。没有切换任务库，也没有停止旧版。"));return 2;}
        if(!openInput.isEmpty())return browser_bridge::exchange(existing,openInput.toStdWString(),0x48425440)?0:2;
        PostMessageW(existing,showWindowMessage,0,0);
        return 0;
    }
    std::unique_ptr<continuity::Lock> libraryLock;
    if(linked) {
      libraryLock=std::make_unique<continuity::Lock>(library);
      if(!libraryLock->acquired()){
        auto message=libraryLock->error==ERROR_SHARING_VIOLATION?QObject::tr("请先正常关闭正在使用此任务库的旧版，再打开统一入口。没有修改原任务库。"):QObject::tr("无法安全打开原任务库锁文件（错误 %1）。请检查路径和访问权限；没有修改原任务库。").arg(libraryLock->error);
        QMessageBox::information(nullptr,QObject::tr("任务库暂不可用"),message);return 2;
      }
      try {continuity::backup(QCoreApplication::applicationDirPath(),library);}
      catch(std::exception const&e){QMessageBox::warning(nullptr,QObject::tr("备份未完成"),QString::fromUtf8(e.what()));return 4;}
    }
    DownloadService service(library);
    receiver=[&](COPYDATASTRUCT*d)->LRESULT{
        if(!d||!d->lpData||d->cbData<2||d->cbData>65536||d->cbData%2)return 0;
        auto chars=static_cast<wchar_t*>(d->lpData);if(chars[d->cbData/2-1]!=0)return 0;
        auto text=QString::fromWCharArray(chars,int(d->cbData/2-1));
        if(d->dwData==0x48425438)return service.externalStatus(text.toInt());
        if(d->dwData==0x48425440)return service.external(text,true);
        if(d->dwData!=0x48425437&&d->dwData!=0x48425439)return 0;
        QString filename;
        if(d->dwData==0x48425439){auto o=QJsonDocument::fromJson(text.toUtf8()).object();if(o.contains("session"))return 0;filename=o.value("filename").toString();text=o.value("url").toString();}
        try{text=QString::fromStdWString(browser_bridge::normalize(text.toStdWString()));}catch(...){return 0;}
        return service.external(text,false,filename);
    };
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    QNetworkAccessManager updateNetwork;
    Updater updater(updateNetwork);
    QQmlApplicationEngine engine;
    QObject::connect(&updater, &Updater::installationReady, &service, &DownloadService::shutdown);
    engine.rootContext()->setContextProperty("appUpdater",&updater);
    engine.rootContext()->setContextProperty("downloadService",&service);
    engine.loadFromModule("HandmadeBT","Main");
    if (engine.rootObjects().isEmpty()) return 1;
    auto window=qobject_cast<QWindow*>(engine.rootObjects().first());
    restoreWindow=[window]{
        if(window->windowState()==Qt::WindowMinimized)window->showNormal();
        else window->show();
        window->raise();window->requestActivate();
    };
    QMenu trayMenu;
    trayMenu.addAction(QObject::tr("显示主窗口"),&app,[]{restoreWindow();});
    trayMenu.addSeparator();
    trayMenu.addAction(QObject::tr("退出鹅鹅下载"),&app,[window]{QMetaObject::invokeMethod(window,"exitApplication");});
    QSystemTrayIcon tray(app.windowIcon());
    tray.setToolTip(QObject::tr("鹅鹅下载"));
    tray.setContextMenu(&trayMenu);
    QObject::connect(&tray,&QSystemTrayIcon::activated,&app,[](QSystemTrayIcon::ActivationReason reason){
        if(reason==QSystemTrayIcon::Trigger||reason==QSystemTrayIcon::DoubleClick)restoreWindow();
    });
    tray.show();
    if(libraryLock)libraryLock->release();
    service.start();
    // The native host treats discovery of this window as readiness for a create request.
    WNDCLASSW wc{};wc.lpfnWndProc=bridgeProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=cls.c_str();RegisterClassW(&wc);
    auto bridge=CreateWindowW(cls.c_str(),L"Eego bridge",0,0,0,0,0,nullptr,nullptr,wc.hInstance,nullptr);
    if(!bridge)return 3;
    if(!openInput.isEmpty())service.importTorrent(openInput);
    ReleaseMutex(gate.h);gate.held=false;
    if (!options.isSet("data-dir")) QTimer::singleShot(1500, &updater, [&updater]{ updater.check(false); });
    const int result = app.exec();
    if (!updater.launchInstaller()) {
        QMessageBox::warning(nullptr,QObject::tr("更新未启动"),QObject::tr("Windows 无法启动安装程序，请重新打开 Eego 后重试。"));
    }
    return result;
}
#else
#ifdef EEGO_TASK_UI_TEST
#include "task_ui_test.inc"
#elif defined(EEGO_UNIFIED_SERVICE_TEST)
#include "unified_service_test.inc"
#else
#include "service_test.inc"
#endif
#endif
#include "main.moc"
