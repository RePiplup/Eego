#pragma once
#include <QFile>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <windows.h>
namespace continuity {
struct Lock {
 HANDLE handle=INVALID_HANDLE_VALUE;DWORD error=0;
 explicit Lock(QString const&library) {handle=CreateFileW((QDir(library).filePath("ui.lock")).toStdWString().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(!acquired())error=GetLastError();}
 ~Lock(){release();}
 bool acquired()const{return handle!=INVALID_HANDLE_VALUE;}
 void release(){if(acquired()){CloseHandle(handle);handle=INVALID_HANDLE_VALUE;}}
};
inline bool sameApplication(HWND window) {
 DWORD pid=0;GetWindowThreadProcessId(window,&pid);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
 if(!process)return false;wchar_t path[32768];DWORD length=32768;bool read=QueryFullProcessImageNameW(process,0,path,&length);CloseHandle(process);
 return read&&QString::compare(QDir::cleanPath(QString::fromWCharArray(path,length)),QDir::cleanPath(QCoreApplication::applicationFilePath()),Qt::CaseInsensitive)==0;
}
inline QString backup(QString const&home,QString const&library) {
 QString marker=QDir(home).filePath("library-continuity.json");
 if(QFile::exists(marker)) {
  QFile file(marker);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("cannot read continuity receipt");
  auto saved=QJsonDocument::fromJson(file.readAll()).object();
  auto destination=saved.value("backup").toString();auto expectedRoot=QFileInfo(QDir(home).filePath("rollback")).canonicalFilePath();auto actual=QFileInfo(destination).canonicalFilePath();auto hashes=saved.value("files").toObject();
  if(saved.value("library").toString()==library&&!expectedRoot.isEmpty()&&actual.startsWith(expectedRoot+'/',Qt::CaseInsensitive)&&hashes.contains("tasks.record")) {
    for(auto it=hashes.begin();it!=hashes.end();++it) {
      if(it.key().contains('/')||it.key().contains('\\')||it.key()==".."||!it.value().isString())throw std::runtime_error("invalid backup manifest");
      QFileInfo info(QDir(destination).filePath(it.key()));QFile original(info.absoluteFilePath());
      if(!info.isFile()||info.isSymLink()||!original.open(QIODevice::ReadOnly))throw std::runtime_error("backup file missing; original preserved");
      auto bytes=original.readAll();if(original.error()!=QFile::NoError||QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())!=it.value().toString())throw std::runtime_error("backup checksum mismatch; original preserved");
    }
    return destination;
  }
  throw std::runtime_error("continuity receipt mismatch; original preserved");
 }
 auto destination=QDir(home).filePath("rollback/"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz"));
 if(!QDir().mkpath(destination))throw std::runtime_error("cannot create continuity backup");
 QJsonObject hashes;
 for(auto const&info:QDir(library).entryInfoList(QDir::Files|QDir::NoDotAndDotDot)) {
  auto name=info.fileName();if(!(name.endsWith(".record")||name.endsWith(".ini")||name=="dht-peers.txt"||name=="dht-bootstrap.txt"))continue;
  if(info.isSymLink())throw std::runtime_error("linked metadata refused");
  QFile file(info.absoluteFilePath());if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("cannot read library metadata");
  auto bytes=file.readAll();if(file.error()!=QFile::NoError)throw std::runtime_error("cannot finish metadata read");
  QSaveFile copy(QDir(destination).filePath(name));if(!copy.open(QIODevice::WriteOnly)||copy.write(bytes)!=bytes.size()||!copy.commit())throw std::runtime_error("cannot save continuity backup");
  hashes.insert(name,QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));
 }
 if(!hashes.contains("tasks.record"))throw std::runtime_error("existing task list missing; original preserved");
 QJsonObject receipt{{"library",library},{"backup",destination},{"files",hashes},{"scope","metadata only; downloads stay at their original paths"}};
 QSaveFile save(marker);auto bytes=QJsonDocument(receipt).toJson();if(!save.open(QIODevice::WriteOnly)||save.write(bytes)!=bytes.size()||!save.commit())throw std::runtime_error("cannot commit continuity receipt");
 return destination;
}
}
