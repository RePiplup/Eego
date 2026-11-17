#pragma once
#include "checkpoint.hpp"
#include <filesystem>
#include <sstream>
#include <vector>
#include <algorithm>

namespace libraries {
namespace fs=std::filesystem;
struct Library {
  fs::path path;
  size_t count;
  fs::file_time_type modified;
};

inline fs::path shared_root(fs::path const &home) {
  return home;
}
inline bool same(fs::path const &left,fs::path const &right) {
  return CompareStringOrdinal(left.c_str(),-1,right.c_str(),-1,TRUE)==CSTR_EQUAL;
}
inline std::vector<Library> discover(fs::path const &home) {
  std::vector<Library> found;
  auto add=[&](fs::path const &dir) {
    try {
      if(std::any_of(found.begin(),found.end(),[&](auto const &l){return same(l.path,dir);}))return;
      auto record=dir/L"tasks.record";
      if(!fs::is_regular_file(record))return;
      std::istringstream in(core::load_record(record));std::string magic;int next;size_t count;
      if(!(in>>magic>>next>>count)||(magic!="GUI1"&&magic!="GUI2")||count>500||next<1)return;
      found.push_back({dir,count,fs::last_write_time(record)});
    }catch(std::exception const&){/* Incomplete or unreadable entries can still be opened explicitly. */}
  };
  add(home/L"userdata");
  add(shared_root(home)/L"userdata");
  std::error_code ec;
  for(fs::directory_iterator it(home.parent_path(),ec),end;!ec&&it!=end;it.increment(ec)){
    auto path=it->path();
    if(path.filename().wstring().rfind(L"HandmadeBT-",0)==0)add(path/L"userdata");
  }
  std::sort(found.begin(),found.end(),[](auto const &a,auto const &b){
    if((a.count>0)!=(b.count>0))return a.count>0;
    return a.modified>b.modified;
  });
  return found;
}
inline fs::path initial(fs::path const &home) {
  auto shared=shared_root(home);
  auto pref=shared/L"active-library.record";
  if(fs::exists(pref)){
    auto path=fs::u8path(core::load_record(pref));
    if(path.is_absolute()&&fs::is_directory(path))return path;
  }
  return shared/L"userdata";
}
inline void remember(fs::path const &home,fs::path const &path) {
  auto shared=shared_root(home);
  fs::create_directories(shared);
  auto file=shared/L"active-library.record";
  if(fs::exists(file)&&core::load_record(file)==path.u8string())return;
  core::save_record(file,path.u8string());
}
}
