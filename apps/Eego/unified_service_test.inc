#include <QFile>
#include <QThread>
#include <iostream>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(argc!=6)return 2;
 auto library=QString::fromLocal8Bit(argv[1]),hostPath=QString::fromLocal8Bit(argv[2]),url=QString::fromLocal8Bit(argv[3]),defaultPath=QString::fromLocal8Bit(argv[4]),oneTime=QString::fromLocal8Bit(argv[5]);
 DownloadService service(library);QString error;
 QObject::connect(&service,&DownloadService::problem,[&](QString s){error=s;});
 service.setDefaultDirectory(defaultPath);if(service.downloadPath()!=defaultPath||!error.isEmpty())return 3;
 service.testHost(hostPath,{"--data-dir",library});service.start();
 auto create=[&](QString destination){int id=0;QEventLoop loop;auto conn=QObject::connect(&service,&DownloadService::created,[&](int value){id=value;loop.quit();});QTimer::singleShot(15000,&loop,&QEventLoop::quit);service.create(url,destination);loop.exec();QObject::disconnect(conn);return id;};
 int first=create(oneTime),second=create({});if(first<1||second<=first)return 4;
 auto tasks=service.transact({{"op","list"}}).value("tasks").toArray();if(tasks.size()!=2)return 5;
 if(!tasks[0].toObject().value("path").toString().replace('\\','/').startsWith(oneTime+'/')||!tasks[1].toObject().value("path").toString().replace('\\','/').startsWith(defaultPath+'/'))return 6;
 QSettings config(library+"/preferences.ini",QSettings::IniFormat);if(config.value("downloadPath").toString()!=defaultPath)return 7;
 DownloadService reopened(library);if(reopened.downloadPath()!=defaultPath)return 8;
 std::cout<<"PASS default directory saved/reopened; one-time destination isolated; subsequent create uses default"<<std::endl;
 service.shutdown();QTimer::singleShot(15000,&app,&QCoreApplication::quit);return app.exec();
}
