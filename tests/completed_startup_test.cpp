#include "task_service.hpp"
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

uint64_t read_bytes() {
  IO_COUNTERS counters{};
  if (!GetProcessIoCounters(GetCurrentProcess(), &counters)) throw std::runtime_error("IO counters unavailable");
  return counters.ReadTransferCount;
}

int main() try {
  auto root = fs::current_path()/"test-temp"/("completed-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
  fs::create_directories(root);
  std::string data(8*1024*1024, 'a');
  auto hash = core::sha256(data);
  fs::path bt_file;
  {
    core::TaskService service;
    service.data_dir = root;
    auto ids = service.create(L"https://example.invalid/a\nmagnet:?xt=urn:btih:0123456789012345678901234567890123456789", root/"downloads");
    for (int id : ids) {
      auto& task = service.find(id);
      auto path = task.inventory.target;
      task.digest = hash;
      if (task.inventory.magnet) {
        path = path/"files"/"nested"/"data.bin";
        bt_file = path;
        auto manifest = "BTFILES1 1\n\"nested/data.bin\" "+std::to_string(data.size())+" "+hash+"\n";
        fs::create_directories(task.magnet_state);
        core::save_record(task.magnet_state/"files.record",manifest);
        task.digest = core::sha256(manifest);
      }
      fs::create_directories(path.parent_path());
      std::ofstream(path,std::ios::binary).write(data.data(),data.size());
      task.inventory.event(path,{});
      Sleep(30);
      task.inventory.save();
      task.status = 3;
      task.total = task.bytes = data.size();
    }
    service.save_tasks();
  }
  {
    core::TaskService service;
    service.data_dir = root;
    auto before = read_bytes();
    service.load_tasks();
    bt_file=service.find(2).inventory.output;
    auto bytes = read_bytes()-before;
    if (service.find(1).status!=3 || service.find(2).status!=3 || bytes>=data.size())
      throw std::runtime_error("unchanged completed payload reread or rejected");
    std::cout<<"Unchanged HTTP + BT startup: "<<bytes<<" bytes read for "<<2*data.size()<<" payload bytes\n";
  }
  Sleep(30);
  auto original_time = fs::last_write_time(bt_file);
  std::fstream(bt_file,std::ios::in|std::ios::out|std::ios::binary).put('b');
  fs::last_write_time(bt_file,original_time);
  // Without an inventory the HTTP payload must still undergo a full hash.
  fs::remove(root/"ownership-1.record");
  {
    core::TaskService service;
    service.data_dir = root;
    auto before = read_bytes();
    service.load_tasks();
    if (service.find(1).status!=3 || service.find(2).status!=4 || read_bytes()-before < 2*data.size())
      throw std::runtime_error("changed BT file or missing HTTP inventory skipped full verification");
  }
  fs::remove_all(root);
  return 0;
} catch(std::exception const& e) {
  std::cerr<<e.what()<<'\n';
  return 1;
}
