#include "checkpoint.hpp"
#include "file_store.hpp"
#include <atomic>
#include <fstream>
#include <libtorrent/hasher.hpp>
#include <stdexcept>
#include <windows.h>
namespace core {
namespace {
constexpr char prefix[] = "HBT-CHECKPOINT-1\n";
constexpr size_t limit = 8 * 1024 * 1024;
std::atomic<unsigned> sequence{0};
} // namespace
std::string sha256(std::string_view data) {
  lt::hasher256 h;
  h.update({data.data(), static_cast<std::ptrdiff_t>(data.size())});
  auto digest = h.final();
  std::string result;
  const char *hex = "0123456789abcdef";
  for (unsigned char c : digest) {
    result += hex[c >> 4];
    result += hex[c & 15];
  }
  return result;
}
std::string load_record(std::filesystem::path const &path) {
  auto size = std::filesystem::file_size(path);
  if (size > limit || size < sizeof(prefix) - 1 + 73)
    throw std::runtime_error("invalid checkpoint size");
  std::ifstream f(path, std::ios::binary);
  std::string data(size, '\0');
  if (!f.read(data.data(), data.size()) || f.peek() != EOF)
    throw std::runtime_error("checkpoint read failed");
  if (data.compare(0, sizeof(prefix) - 1, prefix) != 0 ||
      data.compare(data.size() - 73, 8, "\nSHA256 ") != 0 ||
      data.back() != '\n')
    throw std::runtime_error("invalid checkpoint format (legacy records "
                             "require explicit migration)");
  auto payload =
      data.substr(sizeof(prefix) - 1, data.size() - (sizeof(prefix) - 1) - 73);
  if (sha256(payload) != data.substr(data.size() - 65, 64))
    throw std::runtime_error("checkpoint checksum mismatch");
  return payload;
}
void save_record(std::filesystem::path const &path,
                 std::string const &payload) {
  std::string record =
      std::string(prefix) + payload + "\nSHA256 " + sha256(payload) + "\n";
  if (record.size() > limit)
    throw std::runtime_error("checkpoint too large");
  auto tmp = path;
  tmp += ".tmp-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(sequence++);
  FileStore::prepare(tmp, record.size(), false);
  FileStore::write(tmp, 0, record.data(), record.size());
  FileStore::flush(tmp);
  // Readers (including virus scanners) may briefly deny delete sharing.
  // Keep the old checkpoint intact and retry only this transient conflict.
  for (unsigned attempt = 0;; ++attempt) {
    if (MoveFileExW(tmp.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      { FileStore::notify_created(path,tmp); break; }
    auto error = GetLastError();
    // Windows may report ACCESS_DENIED for replacement of an open target too.
    if ((error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION &&
         error != ERROR_ACCESS_DENIED) ||
        attempt == 20)
      throw std::runtime_error("checkpoint atomic replacement failed: " +
                               std::to_string(error));
    Sleep(25);
  }
}
TaskLock::TaskLock(std::filesystem::path const &path, bool cleanup_access)
    : cleanup_access_(cleanup_access) {
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
  handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE |
                        (cleanup_access ? DELETE : 0), 0, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle_ == INVALID_HANDLE_VALUE)
    throw std::runtime_error("task already in use or lock unavailable: " +
                             std::to_string(GetLastError()));
}
void TaskLock::remove_on_release() {
  // Delete the exact exclusively held lock, never close then unlink by pathname.
  // A nonempty/replaced/link file is not disposable task-lock metadata.
  BY_HANDLE_FILE_INFORMATION info{};
  if (!cleanup_access_ || !GetFileInformationByHandle(handle_, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      info.nNumberOfLinks != 1 || info.nFileSizeHigh || info.nFileSizeLow)
    throw std::runtime_error("lock cleanup refused: not an empty regular single-link task lock");
  FILE_DISPOSITION_INFO disposition{};
  disposition.DeleteFile = TRUE;
  if (!SetFileInformationByHandle(handle_, FileDispositionInfo, &disposition, sizeof(disposition)))
    throw std::runtime_error("task lock cleanup failed: " + std::to_string(GetLastError()));
}
TaskLock::~TaskLock() { CloseHandle(handle_); }
} // namespace core
