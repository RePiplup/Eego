#pragma once
#include "bt_files.hpp"
#include <sstream>
namespace managed {
namespace fs=std::filesystem;
inline std::string identity(fs::path const&p, bool stamp=true, LONGLONG* change_time=nullptr) {
  core::reject_bt_reparse(p);
  auto h=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("无法确认文件身份，已停止删除");
  BY_HANDLE_FILE_INFORMATION i{};bool ok=GetFileInformationByHandle(h,&i);
  if(change_time){
    FILE_BASIC_INFO info{};
    *change_time=GetFileInformationByHandleEx(h,FileBasicInfo,&info,sizeof(info))?info.ChangeTime.QuadPart:0;
  }
  CloseHandle(h);
  if(!ok||(i.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||i.nNumberOfLinks!=1)throw std::runtime_error("目录、链接或文件身份异常，已停止删除");
  std::ostringstream s;s<<i.dwVolumeSerialNumber<<':'<<i.nFileIndexHigh<<':'<<i.nFileIndexLow;
  if(stamp)s<<':'<<i.nFileSizeHigh<<':'<<i.nFileSizeLow<<':'<<i.ftLastWriteTime.dwHighDateTime<<':'<<i.ftLastWriteTime.dwLowDateTime;
  return s.str();
}
}
