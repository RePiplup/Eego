#pragma once
#include "bt_files.hpp"
#include "checkpoint.hpp"

namespace core {
// The old numbered path remains the task's stable binding in recovery records.
// Only a validated leaf in the same download directory may replace the torrent root.
struct OutputName {
  std::filesystem::path leaf;
  bool single=false;
};
inline OutputName output_name(std::filesystem::path const& state) {
  if(state.empty())return {};
  auto record=state/L"output.record";
  reject_bt_reparse(record);
  if(!std::filesystem::exists(record))return {};
  auto bytes=load_record(record);
  bool single=bytes.rfind("FILE\n",0)==0;
  if(!single&&bytes.rfind("TREE\n",0)!=0)throw std::runtime_error("invalid saved output type");
  bytes=bytes.substr(5);
  auto name=std::filesystem::u8path(bytes);
  if(bytes.empty() || bytes.find('\0')!=bytes.npos || name==L"." || name==L".." || name!=name.filename() || name.has_root_path())
    throw std::runtime_error("invalid saved output name");
  libtorrent::file_storage check;check.add_file(bytes,0);validate_bt_files(check);
  return {name,single};
}
inline std::filesystem::path named_relative(std::filesystem::path const& path,std::filesystem::path const& leaf) {
  if(path.empty()||path.has_root_path())throw std::runtime_error("invalid torrent path");
  auto out=leaf;
  for(auto it=std::next(path.begin());it!=path.end();++it){
    if(*it==L".."||*it==L"."||it->empty())throw std::runtime_error("invalid torrent component");
    out/=*it;
  }
  return out;
}
}
