#pragma once
#include <memory>
#include <functional>
#include <filesystem>
#include <vector>
#include <string>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <mutex>
namespace core {
struct BrowserAuthorizationError : std::runtime_error { using std::runtime_error::runtime_error; };
class FileStore;
struct BrowserCookie {std::string name,value,path;bool secure=false;};
struct BrowserSession {std::string url,referer,user_agent;std::vector<BrowserCookie> cookies;};
struct HttpControl {
  mutable std::mutex file_progress_mutex;
  std::vector<int64_t> file_progress;
  bool file_progress_valid=false;
  std::atomic<int> bt_dht_nodes{0},bt_peers{0},bt_candidates{0},bt_tracker_errors{0};
  std::string bt_bootstrap_nodes, bt_node_cache;
  BrowserSession browser_session; // Memory only; never serialized to checkpoints.
  std::atomic<bool> pause{false}, ranged{false};
  std::atomic<uint64_t> total{0}, received{0}, verified{0};
  // 0 connecting, 1 receiving, 2 checking disk, 3 complete, 4 paused.
  std::atomic<int> phase{0};
  std::function<void(std::filesystem::path const&, std::string const&)> before_publish;
  std::string expected_sha256, result_sha256;
};
std::string resolve_bootstrap_json(std::string const &hostname, bool alternate);
int http_download(std::string const &url, std::string const &path, int segments,
                  int stop_after_chunks,
                  std::shared_ptr<FileStore> store = nullptr,
                  HttpControl *control = nullptr);
} // namespace core
