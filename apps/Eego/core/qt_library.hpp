#pragma once
#include "browser_bridge.hpp"
#include <fstream>
namespace qt_library {
inline std::filesystem::path downloads(std::filesystem::path const& library) {
 auto file=library/L"downloads-path.txt";
 if(std::filesystem::exists(file)){
   std::ifstream input(file,std::ios::binary);
   std::string value;std::getline(input,value);
   if(!value.empty()&&value.back()=='\r')value.pop_back();
   auto path=std::filesystem::u8path(value);
   if(!path.is_absolute())throw std::runtime_error("invalid download directory");
   return path;
 }
 return (library.filename()==L"userdata"?library.parent_path():library)/L"Downloads";
}
inline std::filesystem::path resolve(std::filesystem::path const&home) {
 auto link=home/L"library-path.txt";
 if(!std::filesystem::exists(link))return home/L"userdata";
 if(!std::filesystem::is_regular_file(link)||std::filesystem::file_size(link)>32768)throw std::runtime_error("invalid library-path.txt");
 std::ifstream in(link,std::ios::binary);std::string value((std::istreambuf_iterator<char>(in)),{});
 while(!value.empty()&&(value.back()=='\n'||value.back()=='\r'))value.pop_back();
 if(value.empty()||value.find('\0')!=value.npos||value.find_first_of("\r\n")!=value.npos)throw std::runtime_error("invalid library path");
 auto path=std::filesystem::path(browser_bridge::wide(value));
 if(!path.is_absolute()||!std::filesystem::is_directory(path))throw std::runtime_error("linked task library unavailable; original preserved");
 return std::filesystem::absolute(path).lexically_normal();
}
}
