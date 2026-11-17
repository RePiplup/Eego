#include "checkpoint.hpp"
#include <windows.h>
#include <filesystem>
#include <thread>
#include <iostream>
#include <fstream>
int main() {
  auto dir = std::filesystem::temp_directory_path() /
      ("hbt-checkpoint-test-" + std::to_string(GetCurrentProcessId()));
  std::filesystem::create_directories(dir);
  auto p = dir / "state";
  core::save_record(p, "old");
  auto lock = [&] { return CreateFileW(p.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr); };
  HANDLE h = lock();
  if (h == INVALID_HANDLE_VALUE) return 1;
  std::thread reader([h] { Sleep(100); CloseHandle(h); });
  core::save_record(p, "new");
  reader.join();
  if (core::load_record(p) != "new") return 2;
  h = lock();
  if (h == INVALID_HANDLE_VALUE) return 3;
  bool failed = false;
  try { core::save_record(p, "must-not-replace"); }
  catch (...) { failed = true; }
  CloseHandle(h);
  if (!failed || core::load_record(p) != "new") return 4;
  auto lock_path = dir / "cleanup.hbt-lock";
  { core::TaskLock kept(lock_path); }
  if (!std::filesystem::exists(lock_path)) return 5;
  {
    core::TaskLock cleanup(lock_path, true);
    bool blocked = false;
    try { core::TaskLock other(lock_path); } catch (...) { blocked = true; }
    if (!blocked) return 6;
    cleanup.remove_on_release();
    blocked = false;
    try { core::TaskLock other(lock_path); } catch (...) { blocked = true; }
    if (!blocked) return 7;
  }
  if (std::filesystem::exists(lock_path)) return 8;
  { std::ofstream f(lock_path); f << "user data"; }
  {
    core::TaskLock changed(lock_path, true);
    bool refused = false;
    try { changed.remove_on_release(); } catch (...) { refused = true; }
    if (!refused) return 9;
  }
  if (std::filesystem::file_size(lock_path) != 9) return 10;
  auto original = dir / "original.lock", alias = dir / "alias.lock";
  { core::TaskLock empty(original); }
  if (!CreateHardLinkW(alias.c_str(), original.c_str(), nullptr)) return 11;
  {
    core::TaskLock linked(alias, true);
    bool refused = false;
    try { linked.remove_on_release(); } catch (...) { refused = true; }
    if (!refused) return 12;
  }
  if (!std::filesystem::exists(original) || !std::filesystem::exists(alias)) return 13;
  std::cout << "Task-lock cleanup: normal release retained, exclusive delete-on-close, nonempty and hardlink refusal: PASS\n";
  std::filesystem::remove_all(dir);
  std::cout << "Transient reader conflict recovered; persistent lock preserves checkpoint: PASS\n";
}
