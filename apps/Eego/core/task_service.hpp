#pragma once
#include "http.hpp"
#include "magnet.hpp"
#include "managed_files.hpp"
#include <thread>
#include <vector>
#include <memory>
#include <optional>
#include <deque>
#include <map>
#include <functional>
namespace core {
struct ServiceTask {
  int id=0, status=0; // queued, running, paused, complete, error, choose files, no metadata peers
  bool removing=false, delete_files=false;
  managed::Inventory inventory;
  std::filesystem::path magnet_state;
  ULONGLONG sampled=0;uint64_t sample_bytes=0;double speed=0;
  bool recheck_required=false;
  std::string url,path,expected,digest,error,worker_error;
  bool browser_authorization_needed=false;
  uint64_t bytes=0,total=0;
  core::HttpControl control;
  std::thread worker;
  std::atomic<bool> ended{false};
  int result=1;
  uint64_t observer=0;
};

struct FileSnapshot { int index=0; std::string name,path,diskPath; std::optional<uint64_t> size,completed; bool selected=true; };
struct RecoverySnapshot { std::string state="none",message; bool canResume=false,canRetry=false; };
struct TaskSnapshot {
 int id=0,phase=0;std::string name,url,path,status,digest,errorCode,errorMessage;
 uint64_t bytes=0;std::optional<uint64_t> total;double speed=0;
 bool retryable=false,pause=false,resume=false,retry=false,remove=false,selectFiles=false,pauseRequested=false;
 int dhtNodes=0,peers=0,candidates=0,trackerErrors=0;
 RecoverySnapshot recovery;std::vector<FileSnapshot> files;
};
struct TaskEvent { uint64_t seq=0; std::string type; int taskId=0; std::optional<TaskSnapshot> task; };
struct EventBatch { std::string instanceId; uint64_t latestSeq=0;bool resetRequired=false;std::vector<TaskEvent> events; };

// Single-owner task controller. Both UI frontends use this queue and persistence.
// Call methods from one owner thread; workers only publish through HttpControl/ended.
class TaskService {
public:
  std::unique_ptr<TaskLock> instance_lock;
  TaskService();
  const std::vector<std::unique_ptr<ServiceTask>>& legacy_tasks()const{return tasks_;}
  void clear_stopped_tasks();
  TaskSnapshot snapshot(ServiceTask const&)const;
  std::vector<TaskSnapshot> snapshots()const;
  EventBatch events(uint64_t after,std::string const& instance)const;
  std::string const& instance_id()const{return instance_id_;}
  uint64_t latest_seq()const{return sequence_;}
  void publish_changes(bool force=false);
  void request_remove(int id,bool delete_files);
  bool remove_stopped(std::function<void(std::vector<std::filesystem::path> const&)> recycle);
  std::filesystem::path data_dir;
  int next_id=1;
  bool closing=false,storage_failed=false;
  ~TaskService();
  void init_inventory(ServiceTask&);
  static std::string file_hash(ServiceTask const&);
  void save_tasks();
  void persist();
  void load_tasks();
  void load_settings();
  int max_active_tasks()const{return max_active_tasks_;}
  std::string const& settings_error()const{return settings_error_;}
  void set_max_active_tasks(int value);
  void start_next();
  bool poll();
  void sample();
  std::vector<int> create(std::wstring const& input,std::filesystem::path destination,std::wstring const& suggested=L"");
  void action(int id,std::string const& action);
  void apply_selection(ServiceTask&,std::vector<int> const&);
  static std::vector<MagnetFile> files(ServiceTask const&);
  static bool can_select(ServiceTask const&);
  ServiceTask& find(int id);
  void begin_shutdown();
  void shutdown();
private:
  int max_active_tasks_=1;
  std::string settings_error_;
  std::vector<std::unique_ptr<ServiceTask>> tasks_;
  std::string instance_id_;
  uint64_t sequence_=0,last_publish_=0;
  std::deque<TaskEvent> events_;
  std::map<int,std::string> fingerprints_;
  void emit(TaskEvent event);

};
}
