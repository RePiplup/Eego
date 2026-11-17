#include "file_store.hpp"
#include <stdexcept>
#include <windows.h>
#include <winioctl.h>
namespace core {
std::function<void(std::filesystem::path const &, std::filesystem::path const &)> FileStore::created_or_moved;
namespace {
struct Handle {
  HANDLE h;
  explicit Handle(HANDLE v) : h(v) {}
  Handle(Handle const &) = delete;
  Handle(Handle &&r) : h(r.h) { r.h = INVALID_HANDLE_VALUE; }
  ~Handle() {
    if (h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
  }
};
void check(bool ok) {
  if (!ok)
    throw std::runtime_error("FileStore Windows error " +
                             std::to_string(GetLastError()));
}
Handle open(std::filesystem::path const &p, DWORD mode,
            DWORD creation = OPEN_EXISTING) {
  Handle f{CreateFileW(p.c_str(), mode, FILE_SHARE_READ, nullptr, creation,
                       FILE_ATTRIBUTE_NORMAL, nullptr)};
  check(f.h != INVALID_HANDLE_VALUE);
  return f;
}
void seek(HANDLE h, std::uint64_t off) {
  LARGE_INTEGER pos;
  pos.QuadPart = off;
  check(SetFilePointerEx(h, pos, nullptr, FILE_BEGIN));
}
} // namespace
FileStore::FileStore(int delay)
    : delay_(delay), worker_([this] {
        for (;;) {
          std::function<void()> f;
          {
            std::unique_lock<std::mutex> l(mutex_);
            ready_.wait(l, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty())
              return;
            f = std::move(jobs_.front());
            jobs_.pop();
          }
          f();
        }
      }) {}
FileStore::~FileStore() {
  {
    std::lock_guard<std::mutex> l(mutex_);
    stopping_ = true;
  }
  ready_.notify_all();
  worker_.join();
}
void FileStore::enqueue(std::function<void()> f) {
  {
    std::lock_guard<std::mutex> l(mutex_);
    if (stopping_)
      throw std::runtime_error("store stopped");
    jobs_.push(std::move(f));
  }
  ready_.notify_one();
}
void FileStore::barrier() {
  auto p = std::make_shared<std::promise<void>>();
  auto f = p->get_future();
  enqueue([p] { p->set_value(); });
  f.get();
}
void FileStore::prepare(std::filesystem::path const &p, std::uint64_t size,
                        bool existing, bool sparse) {
  if (existing) {
    if (!std::filesystem::exists(p) || std::filesystem::file_size(p) != size)
      throw std::runtime_error("resume file size mismatch");
    return;
  }
  if (!p.parent_path().empty())
    std::filesystem::create_directories(p.parent_path());
  auto f = open(p, GENERIC_WRITE, CREATE_NEW);
  if(sparse) { DWORD returned=0; check(DeviceIoControl(f.h,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&returned,nullptr)); }
  seek(f.h, size);
  check(SetEndOfFile(f.h));
  if(created_or_moved)created_or_moved(p, {});
}
void FileStore::write(std::filesystem::path const &p, std::uint64_t off,
                      char const *b, size_t n) {
  auto f = open(p, GENERIC_WRITE);
  seek(f.h, off);
  while (n) {
    DWORD w = 0;
    check(WriteFile(f.h, b, DWORD(std::min<size_t>(n, 1 << 20)), &w, nullptr));
    if (!w)
      throw std::runtime_error("zero write");
    b += w;
    n -= w;
  }
}
std::vector<char> FileStore::read(std::filesystem::path const &p,
                                  std::uint64_t off, size_t n) {
  auto f = open(p, GENERIC_READ);
  seek(f.h, off);
  std::vector<char> b(n);
  size_t at = 0;
  while (at < n) {
    DWORD r = 0;
    check(ReadFile(f.h, b.data() + at, DWORD(std::min<size_t>(n - at, 1 << 20)),
                   &r, nullptr));
    if (!r)
      throw std::runtime_error("short disk read");
    at += r;
  }
  return b;
}
void FileStore::truncate(std::filesystem::path const &p) {
  auto f=open(p, GENERIC_WRITE);
  seek(f.h,0);
  check(SetEndOfFile(f.h));
}
void FileStore::flush(std::filesystem::path const &p) {
  auto f = open(p, GENERIC_WRITE);
  check(FlushFileBuffers(f.h));
}
void FileStore::write_wait(std::filesystem::path const &p, std::uint64_t off,
                           std::vector<char> b) {
  auto done = std::make_shared<std::promise<void>>();
  auto f = done->get_future();
  enqueue([this, p, off, b = std::move(b), done] {
    try {
      if (delay_)
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_));
      write(p, off, b.data(), b.size());
      ++writes;
      bytes += b.size();
      done->set_value();
    } catch (...) {
      done->set_exception(std::current_exception());
    }
  });
  f.get();
}
} // namespace core
