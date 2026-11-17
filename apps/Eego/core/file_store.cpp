#include "file_store.hpp"
#include <stdexcept>
#include <windows.h>
#include <winioctl.h>
#include <map>
namespace core {
namespace {
struct Subscription { FileStore::PathFilter filter; FileStore::Observer callback; std::mutex mutex; };
std::mutex observers_mutex;
std::map<uint64_t,std::shared_ptr<Subscription>> observers;
uint64_t next_observer=0;
}
uint64_t FileStore::observe(PathFilter filter, Observer callback) {
  auto entry=std::make_shared<Subscription>();entry->filter=std::move(filter);entry->callback=std::move(callback);
  std::lock_guard<std::mutex> lock(observers_mutex);
  auto token=++next_observer;observers.emplace(token,std::move(entry));return token;
}
void FileStore::unobserve(uint64_t token) {
  if(!token)return;
  std::shared_ptr<Subscription> entry;
  {std::lock_guard<std::mutex> lock(observers_mutex);auto it=observers.find(token);if(it==observers.end())return;entry=it->second;observers.erase(it);}
  std::lock_guard<std::mutex> lock(entry->mutex);entry->callback={};
}
void FileStore::notify_created(std::filesystem::path const&p,std::filesystem::path const&from) {
  std::vector<std::shared_ptr<Subscription>> matches;
  {std::lock_guard<std::mutex> lock(observers_mutex);for(auto const& item:observers)if(item.second->filter(p))matches.push_back(item.second);}
  // No registry lock while calling inventory code. Nonmatching inventories are
  // never locked (inventory.save itself writes records through FileStore).
  for(auto const& entry:matches){std::lock_guard<std::mutex> lock(entry->mutex);if(entry->callback)entry->callback(p,from);}
}
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
  notify_created(p, {});
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
