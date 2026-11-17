#include <QFile>
#include <iostream>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(argc!=6)return 2;
 DownloadService service(QString::fromLocal8Bit(argv[1]));
 QString error;QObject::connect(&service,&DownloadService::problem,[&](QString s){error=s;});
 auto original=service.downloadPath();
 service.setDefaultDirectory("relative/missing");if(error.isEmpty()||service.downloadPath()!=original)return 3;
 error.clear();service.setDefaultDirectory(QString::fromLocal8Bit(argv[5]));
 if(error.isEmpty()||service.downloadPath()!=original)return 4;
 QSettings config(QString::fromLocal8Bit(argv[1])+"/preferences.ini",QSettings::IniFormat);
 if(config.contains("downloadPath"))return 5;
 service.testHost(QString::fromLocal8Bit(argv[2]),{QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]),QString::fromLocal8Bit(argv[1])});service.start();
 auto first=service.transact({{"op","create"},{"url","http://127.0.0.1:9/first"}});
 if(!first.isEmpty())return 6;
 auto blocked=service.transact({{"op","create"},{"url","http://127.0.0.1:9/blocked"}});if(!blocked.isEmpty())return 7;
 QEventLoop wait;QTimer::singleShot(3500,&wait,&QEventLoop::quit);wait.exec();
 auto next=service.transact({{"op","create"},{"url","http://127.0.0.1:9/second"}});
 if(!next.value("ok").toBool()||next.value("id").toInt()!=2)return 8;
 auto list=service.transact({{"op","list"}});if(list.value("tasks").toArray().size()!=2){std::cout<<QJsonDocument(list).toJson().constData()<<std::endl;return 9;}
 std::cout<<"PASS invalid/unwritable directory signals and unchanged config; delayed create timeout, blocked request, recovered second ID=2; two tasks only"<<std::endl;
 service.shutdown();QTimer::singleShot(10000,&app,&QCoreApplication::quit);return app.exec();
}
