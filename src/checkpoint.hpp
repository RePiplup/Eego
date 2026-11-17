// HandmadeBT, GPL-2.0-or-later. Local crash-recovery primitives.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace core {
std::string sha256(std::string_view bytes);
std::string load_record(std::filesystem::path const &path);
void save_record(std::filesystem::path const &path, std::string const &payload);
class TaskLock {
public:
  explicit TaskLock(std::filesystem::path const &path, bool cleanup_access = false);
  void remove_on_release();
  ~TaskLock();
  TaskLock(TaskLock const &) = delete;
  TaskLock &operator=(TaskLock const &) = delete;

private:
  void *handle_;
  bool cleanup_access_ = false;
};
} // namespace core
