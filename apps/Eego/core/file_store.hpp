#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
namespace core {
// Minimal shared disk executor, used by both HTTP and BT. No libtorrent
// backend.
class FileStore {
public:
  using Observer = std::function<void(std::filesystem::path const &, std::filesystem::path const &)>;
  using PathFilter = std::function<bool(std::filesystem::path const &)>;
  // Filters must be immutable, nonblocking and must not call FileStore.
  // Unregister waits for in-flight notifications before the owner can die.
  static uint64_t observe(PathFilter filter, Observer observer);
  static void unobserve(uint64_t token);
  static void notify_created(std::filesystem::path const &, std::filesystem::path const &);
  explicit FileStore(int delay_ms = 0);
  ~FileStore();
  void enqueue(std::function<void()> job);
  void barrier();
  static void prepare(std::filesystem::path const &, std::uint64_t size,
                      bool existing, bool sparse=false);
  static void write(std::filesystem::path const &, std::uint64_t offset,
                    char const *, size_t);
  static std::vector<char> read(std::filesystem::path const &,
                                std::uint64_t offset, size_t);
  static void truncate(std::filesystem::path const &);
  static void flush(std::filesystem::path const &);
  void write_wait(std::filesystem::path const &, std::uint64_t,
                  std::vector<char>);
  std::atomic<std::uint64_t> writes{0}, bytes{0};

private:
  int delay_;
  bool stopping_ = false;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<std::function<void()>> jobs_;
  std::thread worker_;
};
} // namespace core
