#pragma once
#include "file_store.hpp"
#include <libtorrent/file_storage.hpp>
#include <set>
#include <windows.h>
namespace core {
inline void validate_bt_files(libtorrent::file_storage const &files) {
  struct Less { bool operator()(std::wstring const&a,std::wstring const&b) const {
    return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;
  }};
  std::set<std::wstring,Less> names;
  for(auto i:files.file_range()) {
    if(files.file_flags(i)&libtorrent::file_storage::flag_symlink)
      throw std::runtime_error("torrent symlinks are not supported");
    auto p=std::filesystem::u8path(files.file_path(i));
    if(p.empty()||p.has_root_path())throw std::runtime_error("unsafe torrent path");
    for(auto const &component:p) {
      auto s=component.wstring();
      if(s.empty()||s==L"."||s==L".."||s.back()==L'.'||s.back()==L' '||s.find_first_of(L"<>:\"|?*")!=s.npos)
        throw std::runtime_error("unsafe Windows torrent filename");
      for(auto c:s)if(c<32)throw std::runtime_error("control character in torrent filename");
      auto base=s.substr(0,s.find(L'.'));for(auto &c:base)if(c>=L'a'&&c<=L'z')c-=32;
      if(base==L"CON"||base==L"PRN"||base==L"AUX"||base==L"NUL"||
         (base.size()==4&&(base.substr(0,3)==L"COM"||base.substr(0,3)==L"LPT")&&base[3]>=L'1'&&base[3]<=L'9'))
        throw std::runtime_error("reserved Windows torrent filename");
    }
    if(!names.insert(p.wstring()).second)throw std::runtime_error("duplicate torrent filename");
  }
}
inline void reject_bt_reparse(std::filesystem::path const &p) {
  // Never follow reparse points in an existing path, including ancestors.
  for(auto q=p;!q.empty();) {
    auto attr=GetFileAttributesW(q.c_str());
    if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("文件路径包含目录联接或符号链接，已停止操作以保护文件");
    auto parent=q.parent_path();if(parent==q)break;q=parent;
  }
}
inline std::filesystem::path bt_path(std::filesystem::path const &root,libtorrent::file_storage const &files,libtorrent::file_index_t i,std::vector<bool> const& selected={}, std::filesystem::path const& boundary={}) {
  auto p=!selected.empty()&&!selected.at(int(i)) ? (boundary.empty()?root.parent_path()/L"boundary-cache":boundary)/(std::to_wstring(int(i))+L".part") : root/std::filesystem::u8path(files.file_path(i));reject_bt_reparse(p);return p;
}
inline std::vector<char> bt_read(std::filesystem::path const&root,libtorrent::file_storage const&files,
                               libtorrent::piece_index_t piece,int offset,int size,std::vector<bool> const& selected={}, std::filesystem::path const& boundary={}) {
  std::vector<char> out;out.reserve(size);
  for(auto const &s:files.map_block(piece,offset,size)) {
    if(files.pad_file_at(s.file_index))out.insert(out.end(),size_t(s.size),0);
    else {
      auto p=bt_path(root,files,s.file_index,selected,boundary);
      if(!selected.empty()&&!selected.at(int(s.file_index))&&!std::filesystem::exists(p))out.insert(out.end(),size_t(s.size),0);
      else {auto b=FileStore::read(p,s.offset,size_t(s.size));out.insert(out.end(),b.begin(),b.end());}
    }
  }
  if(out.size()!=size_t(size))throw std::runtime_error("BT block mapping mismatch");
  return out;
}
inline void bt_write(std::filesystem::path const&root,libtorrent::file_storage const&files,
                     libtorrent::piece_index_t piece,int offset,char const*data,int size,std::vector<bool> const& selected={}, std::filesystem::path const& boundary={}) {
  int64_t consumed=0;
  for(auto const &s:files.map_block(piece,offset,size)) {
    if(!files.pad_file_at(s.file_index)) {
      auto p=bt_path(root,files,s.file_index,selected,boundary);
      if(!selected.empty()&&!selected.at(int(s.file_index)))FileStore::prepare(p,files.file_size(s.file_index),std::filesystem::exists(p),true);
      FileStore::write(p,s.offset,data+consumed,size_t(s.size));
    }
    consumed+=s.size;
  }
  if(consumed!=size)throw std::runtime_error("BT block mapping mismatch");
}
inline void bt_flush(std::filesystem::path const&root,libtorrent::file_storage const&files,std::vector<bool> const& selected={}, std::filesystem::path const& boundary={}) {
  for(auto i:files.file_range())if(!files.pad_file_at(i)) {
    auto p=bt_path(root,files,i,selected,boundary);
    if(selected.empty()||selected.at(int(i))||std::filesystem::exists(p))FileStore::flush(p);
  }
}
}
