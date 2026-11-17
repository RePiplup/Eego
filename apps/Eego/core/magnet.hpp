#pragma once
#include "http.hpp"
#include <vector>
namespace core {
// Uses the same storage backend and mature libtorrent peer/piece algorithms.
int magnet_download(std::string const &uri,std::string const &directory,HttpControl &control,bool choose_files=false, std::filesystem::path state={});
struct MagnetFile { int index; std::string path; uint64_t size; bool selected; };
std::vector<MagnetFile> magnet_files(std::string const& directory, std::filesystem::path state={});
void select_magnet_files(std::string const& directory,std::vector<int> const& indexes, std::filesystem::path state={});
std::string verify_magnet_output(std::string const &directory, std::filesystem::path state={},
    std::function<bool(std::filesystem::path const&)> const& previously_verified={});
}
