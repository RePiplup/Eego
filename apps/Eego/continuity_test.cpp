#include <QCoreApplication>
#include "library_continuity.hpp"
#include <iostream>
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;QString root=QString::fromLocal8Bit(argv[1]),library=root+"/old",home=root+"/new";QDir().mkpath(library);QDir().mkpath(home);
 auto write=[](QString const&p,QByteArray const&b){QFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(b)!=b.size())throw std::runtime_error("fixture write");};
 try{
 write(library+"/ui.lock",{});write(library+"/tasks.record","old-task-fixture");write(library+"/preferences.ini","[General]\ndownloadPath=X:/old\n");
 continuity::Lock first(library);if(!first.acquired())return 3;{continuity::Lock blocked(library);if(blocked.acquired())return 4;}
 auto backup=continuity::backup(home,library);QFile f(backup+"/tasks.record");if(!f.open(QIODevice::ReadOnly)||f.readAll()!="old-task-fixture")return 5;
 if(continuity::backup(home,library)!=backup)return 6;
 write(backup+"/tasks.record","damaged");bool refused=false;try{continuity::backup(home,library);}catch(...){refused=true;}if(!refused)return 9;
 first.release();continuity::Lock second(library);if(!second.acquired())return 7;
 QFile original(library+"/tasks.record");if(!original.open(QIODevice::ReadOnly)||original.readAll()!="old-task-fixture")return 8;
 std::cout<<"PASS exclusive readiness probe, metadata backup exact, idempotent receipt, corrupted backup refused, old metadata unchanged"<<std::endl;return 0;
 }catch(std::exception const&e){std::cerr<<e.what();return 1;}}
