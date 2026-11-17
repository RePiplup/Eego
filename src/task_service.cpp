#include "task_service.hpp"
#include "file_store.hpp"
#include "browser_bridge.hpp"
#include <libtorrent/hasher.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <set>
namespace core {
namespace fs=std::filesystem;
using Task=ServiceTask;
std::string TaskService::file_hash(Task const &t) {
  if(t.url.rfind("magnet:?",0)==0)return core::verify_magnet_output(t.path);
  auto p=fs::u8path(t.path);
  if(fs::file_size(p)!=t.total) throw std::runtime_error("completed file size changed");
  lt::hasher256 hash;
  for(uint64_t at=0;at<t.total;) {
    auto b=core::FileStore::read(p,at,size_t(std::min<uint64_t>(1048576,t.total-at)));
    hash.update({b.data(),static_cast<std::ptrdiff_t>(b.size())});at+=b.size();
  }
  std::string s;char const *hex="0123456789abcdef";
  for(unsigned char c:hash.final()){s+=hex[c>>4];s+=hex[c&15];}return s;
}
void TaskService::init_inventory(Task &t) {
  t.inventory.target=fs::absolute(fs::u8path(t.path)).lexically_normal();
  t.inventory.magnet=t.url.rfind("magnet:?",0)==0;
  t.inventory.record=data_dir/(L"ownership-"+std::to_wstring(t.id)+L".record");
  t.inventory.binding=std::to_string(t.id)+"\n"+t.url+"\n"+t.path;
  t.inventory.load();
}
namespace {
fs::path receipt(Task const& t) {
  return t.inventory.record.parent_path() / ("completion-" + std::to_string(t.id) + ".record");
}
bool owned(Task const& t, fs::path const& p) {
  auto it=t.inventory.entries.find(p);
  return it!=t.inventory.entries.end() && it->second==managed::identity(p);
}
// The HTTP file ID survives the final rename. For BT the receipt binds the
// verified file manifest to this task and its verified torrent metadata.
bool recover_published(Task& t) {
  bool bt=t.inventory.magnet;
  auto root=t.inventory.target;
  auto final=bt ? root/L"files.record" : root;
  if(!fs::exists(final))return false;
  auto anchor=bt ? root/L"metadata.record" : final;
  std::string wanted=t.expected;
  if(fs::exists(receipt(t))) {
    std::istringstream in(load_record(receipt(t)));
    std::string binding,id,hash;
    if(!(in>>std::quoted(binding)>>std::quoted(id)>>hash) ||
       binding!=t.inventory.binding || id!=managed::identity(anchor) ||
       hash.size()!=64 || (!wanted.empty() && hash!=wanted))
      throw std::runtime_error("完成凭据或文件身份不符，原文件已保留");
    wanted=hash;
  } else {
    if(!owned(t,final) || (!bt && wanted.empty()))
      throw std::runtime_error("已有最终文件，缺少能核实归属和内容的完成凭据，原文件已保留");
    if(bt)wanted=sha256(load_record(final));
  }
  if(bt) {
    auto info=lt::parse_magnet_uri(t.url);
    std::ostringstream owner;owner<<info.info_hashes.v1<<'\n'<<root.u8string();
    if(load_record(root/L"magnet.owner")!=owner.str())
      throw std::runtime_error("磁力任务与文件归属不符，原文件已保留");
    t.total=0;
    for(auto const& file:magnet_files(t.path))if(file.selected) {
      if(!owned(t,root/L"files"/fs::u8path(file.path)))
        throw std::runtime_error("磁力文件身份未能核实，原文件已保留");
      t.total+=file.size;
    }
  } else t.total=fs::file_size(final);
  auto actual=TaskService::file_hash(t);
  if(actual!=wanted)throw std::runtime_error("最终文件内容校验失败，原文件已保留");
  t.inventory.event(final,{});
  t.inventory.save();
  t.bytes=t.total;t.digest=actual;t.error.clear();t.status=3;
  return true;
}
}
void TaskService::save_tasks() {
  std::ostringstream out; out<<"GUI2 "<<next_id<<' '<<tasks_.size()<<'\n';
  for(auto const &p:tasks_) {
    auto &t=*p;
    out<<t.id<<' '<<t.status<<' '<<t.bytes<<' '<<t.total<<' '
       <<std::quoted(t.url)<<' '<<std::quoted(t.path)<<' '<<std::quoted(t.expected)<<' '
       <<std::quoted(t.digest)<<' '<<std::quoted(t.error)<<'\n';
  }
  core::save_record(data_dir/L"tasks.record",out.str());
}
void TaskService::load_tasks() {
  auto p=data_dir/L"tasks.record";if(!fs::exists(p))return;
  std::istringstream in(core::load_record(p));std::string magic;size_t count;
  if(!(in>>magic>>next_id>>count)||(magic!="GUI1"&&magic!="GUI2")||count>500||next_id<1)
    throw std::runtime_error("invalid task list; original data preserved");
  bool reconciled=false;
  int previous=0;
  for(size_t i=0;i<count;++i) {
    auto t=std::make_unique<Task>();
    if(!(in>>t->id>>t->status>>t->bytes>>t->total>>std::quoted(t->url)>>std::quoted(t->path)
         >>std::quoted(t->expected)>>std::quoted(t->digest)>>std::quoted(t->error)) ||
       t->id<=previous || t->id>=next_id || t->status<0 || t->status>6)
      throw std::runtime_error("invalid task record; original data preserved");
    previous=t->id;
    if(t->status==0||t->status==1) { t->status=2; t->error="重开后已暂停，点击恢复继续"; }
    if(t->status==3) {
      try {if(t->digest.empty()||file_hash(*t)!=t->digest)throw std::runtime_error("completed file hash changed");}
      catch(std::exception const &e){t->status=4;t->error=e.what();}
    }
    if(t->url.rfind("magnet:?",0)==0&&fs::exists(fs::u8path(t->path)/L"metadata.record")) {
      try {uint64_t selected_total=0;for(auto const&f:magnet_files(t->path))if(f.selected)selected_total+=f.size;t->total=selected_total;}
      catch(std::exception const&e){t->status=4;t->error=e.what();}
    }
    init_inventory(*t);
    if(t->status==2 || (t->status==4 && t->error=="output exists; refusing overwrite")) {
      try { if(recover_published(*t))reconciled=true; }
      catch(std::exception const& e){t->status=4;t->error=e.what();reconciled=true;}
    }
    t->recheck_required=t->status==2;
    tasks_.push_back(std::move(t));
  }
  in>>std::ws;if(!in.eof())throw std::runtime_error("trailing task-list data");
  if(reconciled)save_tasks();
}
void TaskService::persist() {
  try { save_tasks(); } catch(...) { storage_failed=true; for(auto &t:tasks_)if(t->status==1)t->control.pause=true; throw; }
}
void TaskService::start_next() {
  if(closing||storage_failed)return;
  for(auto const &t:tasks_)if(t->status==1)return;
  for(auto &p:tasks_)if(p->status==0&&!p->removing) {
    auto t=p.get();
    try {
    try { if(recover_published(*t)){persist();continue;} }
    catch(std::exception const& e){t->status=4;t->error=e.what();persist();continue;}
    t->status=1;t->error.clear();t->worker_error.clear();t->ended=false;
    t->browser_authorization_needed=false;
    t->control.pause=false;t->control.phase=0;t->control.received=0;t->control.total=0;
    t->control.verified=0;t->control.ranged=false;t->control.result_sha256.clear();
    {std::lock_guard<std::mutex> lock(t->control.file_progress_mutex);t->control.file_progress.clear();t->control.file_progress_valid=false;}
    t->control.expected_sha256=t->expected;
    t->control.bt_bootstrap_nodes.clear();
    t->control.bt_node_cache=(data_dir/L"dht-peers.txt").u8string();
    t->control.bt_dht_nodes=0;t->control.bt_peers=0;t->control.bt_candidates=0;t->control.bt_tracker_errors=0;
    {std::ifstream config(data_dir/L"dht-bootstrap.txt");std::getline(config,t->control.bt_bootstrap_nodes);if(!t->control.bt_bootstrap_nodes.empty()&&t->control.bt_bootstrap_nodes.back()=='\r')t->control.bt_bootstrap_nodes.pop_back();}
    persist();if(storage_failed){t->status=2;return;}
    t->inventory.begin();t->sampled=0;t->speed=0;
    core::FileStore::created_or_moved=[t](auto const&p,auto const&from){t->inventory.event(p,from);};
    t->control.before_publish=[t](fs::path const& file,std::string const& digest){
      t->inventory.save();
      std::ostringstream out;
      out<<std::quoted(t->inventory.binding)<<' '<<std::quoted(managed::identity(file))<<' '<<digest;
      save_record(receipt(*t),out.str());
    };
    t->worker=std::thread([t] {
      try {
        if(t->url.rfind("magnet:?",0)==0)t->result=core::magnet_download(t->url,t->path,t->control,true);
        else t->result=core::http_download(t->url,t->path,4,0,nullptr,&t->control);
      }
      catch(core::BrowserAuthorizationError const &e){t->worker_error=e.what();t->browser_authorization_needed=true;t->result=1;}
      catch(std::exception const &e){t->worker_error=e.what();t->result=1;}
      t->ended.store(true,std::memory_order_release);
    });return;
    }catch(std::exception const&e){
      FileStore::created_or_moved={};t->status=4;t->error=e.what();t->worker_error.clear();t->ended=false;
      if(!storage_failed){try{persist();}catch(...){}}
      throw;
    }
  }
}
bool TaskService::poll() {
  bool changed=false,active=false;
  for(auto &p:tasks_)if(p->status==1) {
    auto &t=*p;
    if(t.ended.load(std::memory_order_acquire)) {
      if(t.worker.joinable())t.worker.join();core::FileStore::created_or_moved={};t.bytes=t.control.received;t.total=t.control.total;
      t.error=t.worker_error;
      t.status=t.result==0?3:(t.result==3?2:(t.result==4?5:(t.result==5?6:4)));
      if(t.status==6)t.error="5 分钟内未找到可提供元数据的节点；可能是节点离线或网络连接受限，不代表资源不存在。可稍后重试。";
      t.digest=t.control.result_sha256;
      if(t.status!=2)t.control.browser_session={}; // Drop credentials after completion/failure.
      try{t.inventory.save();}catch(std::exception const&e){t.error+=std::string("；归属清单保存失败，文件删除将受限：")+e.what();}
      changed=true;
    } else if(t.worker.joinable())active=true;
    else {t.status=4;t.error="worker did not start";changed=true;}
  }
  if(changed&&!storage_failed)persist();
  sample();
  publish_changes();
  return active;
}
std::wstring trim(std::wstring s) {
  auto a=s.find_first_not_of(L" \t\r\n");if(a==s.npos)return {};
  return s.substr(a,s.find_last_not_of(L" \t\r\n")-a+1);
}
std::vector<int> TaskService::create(std::wstring const &external,fs::path destination,std::wstring const &suggested) {
  if(closing||storage_failed)throw std::runtime_error("task service unavailable");
  auto dir=fs::absolute(destination).lexically_normal();
  fs::create_directories(dir);
  std::wistringstream input(external);std::wstring line;
  std::vector<std::unique_ptr<Task>> pending;
  while(std::getline(input,line)) {
    line=trim(line);if(line.empty())continue;
    auto bar=line.find(L'|');auto url=trim(line.substr(0,bar));
    auto expected=bar==line.npos?std::wstring{}:trim(line.substr(bar+1));
    bool magnet=url.rfind(L"magnet:?",0)==0;
    if((!magnet&&url.rfind(L"http://",0)!=0&&url.rfind(L"https://",0)!=0)||url.find_first_of(L" \t\r\n")!=url.npos)
      throw std::runtime_error("请输入 HTTP、HTTPS 或 Magnet 链接；每行一条");
    if(magnet&&!expected.empty())throw std::runtime_error("Magnet 自动按种子哈希校验，无需填写单文件 SHA-256");
    for(auto &c:expected)if(c>=L'A'&&c<=L'F')c+=32;
    if(!expected.empty()&&(expected.size()!=64||expected.find_first_not_of(L"0123456789abcdef")!=expected.npos))
      throw std::runtime_error("可选校验值格式：URL | 64 位 SHA-256");
    if(tasks_.size()+pending.size()>=500)throw std::runtime_error("当前最多保留 500 个任务");
    auto t=std::make_unique<Task>();t->id=next_id+int(pending.size());t->url=browser_bridge::utf8(url);t->expected=expected.empty()?std::string{}:browser_bridge::utf8(expected);
    auto clean=url.substr(0,url.find_first_of(L"?#"));auto slash=clean.find_last_of(L'/');
    auto name=suggested.empty()?clean.substr(slash+1):fs::path(suggested).filename().wstring();if(name.empty())name=L"download.bin";
    if(name.size()>100)name=name.substr(0,100);
    for(auto &c:name)if(c<32||std::wstring(L"<>:\"/\\|?*").find(c)!=std::wstring::npos)c=L'_';
    while(!name.empty()&&(name.back()==L'.'||name.back()==L' '))name.pop_back();
    t->path=(dir/(std::to_wstring(t->id)+L"-"+(magnet?L"magnet":name))).u8string();init_inventory(*t);pending.push_back(std::move(t));
  }
  if(pending.empty())throw std::runtime_error("empty task input");
  std::vector<int> ids;for(auto const&t:pending)ids.push_back(t->id);
  next_id+=int(pending.size());for(auto &t:pending)tasks_.push_back(std::move(t));
  persist();
  publish_changes(true);
  return ids;
}
void TaskService::apply_selection(Task &t,std::vector<int> const&indexes) {
  if(indexes.empty())throw std::runtime_error("select at least one file");
  if(std::set<int>(indexes.begin(),indexes.end()).size()!=indexes.size())throw std::runtime_error("duplicate file index");
  if(closing||storage_failed||t.status==1||t.removing)throw std::runtime_error("file selection unavailable in current state");
  auto path=fs::u8path(t.path)/L"selection.record";bool fresh=!fs::exists(path);
  core::select_magnet_files(t.path,indexes);
  if(fresh)t.inventory.event(path,{});
  t.total=0;for(auto const&f:magnet_files(t.path))if(f.selected)t.total+=f.size;t.bytes=0;
  t.inventory.save();t.status=0;t.error.clear();persist();publish_changes(true);
}

std::vector<MagnetFile> TaskService::files(ServiceTask const&t){return magnet_files(t.path);}
bool TaskService::can_select(ServiceTask const&t){return t.url.rfind("magnet:?",0)==0&&t.status!=1&&!t.removing&&fs::exists(fs::u8path(t.path)/L"metadata.record")&&!fs::exists(fs::u8path(t.path)/L"files");}
ServiceTask& TaskService::find(int id){for(auto&t:tasks_)if(t->id==id)return *t;throw std::runtime_error("task not found");}
void TaskService::action(int id,std::string const&a){
  if(closing||storage_failed)throw std::runtime_error("task service unavailable");
  auto&t=find(id);if(t.removing)throw std::runtime_error("task being removed");
  if(a=="pause"&&(t.status==0||t.status==1)){if(t.status==1)t.control.pause=true;else t.status=2;}
  else if((a=="resume"&&t.status==2)||(a=="retry"&&(t.status==4||t.status==6))){t.status=0;t.error.clear();}
  else throw std::runtime_error("invalid action for task state");
  persist();publish_changes(true);
}
void TaskService::sample(){for(auto&p:tasks_){auto&t=*p;
  if(t.status==1){t.bytes=t.control.received;t.total=t.control.total;if(t.control.phase==1||t.control.phase==3)t.recheck_required=false;}
  auto now=GetTickCount64();if(!t.sampled){t.sampled=now;t.sample_bytes=t.bytes;}
  if(now-t.sampled>=1000){t.speed=t.status==1&&t.control.phase==1&&t.bytes>=t.sample_bytes?1000.0*(t.bytes-t.sample_bytes)/(now-t.sampled):0;t.sampled=now;t.sample_bytes=t.bytes;}
  if(t.status!=1||t.control.phase!=1)t.speed=0;
}}
void TaskService::begin_shutdown(){closing=true;for(auto&t:tasks_){if(t->status==1)t->control.pause=true;else if(t->status==0)t->status=2;}persist();}
void TaskService::shutdown(){
  closing=true;for(auto&t:tasks_)if(t->worker.joinable())t->control.pause=true;
  bool active=true;while(active){try{active=poll();}catch(...){for(auto&t:tasks_)if(t->worker.joinable())t->worker.join();break;}if(active)Sleep(20);}
  FileStore::created_or_moved={};
}
TaskService::~TaskService(){shutdown();}
TaskService::TaskService(){FILETIME ft;GetSystemTimeAsFileTime(&ft);instance_id_=std::to_string(GetCurrentProcessId())+"-"+std::to_string((uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime);}
TaskSnapshot TaskService::snapshot(ServiceTask const&t)const {
 TaskSnapshot s;s.id=t.id;s.name=fs::u8path(t.path).filename().u8string();s.url=t.url;s.path=t.path;s.phase=t.control.phase;
 s.status=t.status==0?"queued":t.status==2?"paused":t.status==3?"completed":t.status==4||t.status==6?"failed":t.status==5?"choosing":s.phase==1?"downloading":"fetching";
 s.bytes=t.bytes;if(t.total)s.total=t.total;s.speed=t.speed;s.digest=t.digest;
 s.pause=(t.status==0||t.status==1)&&!closing&&!storage_failed;s.resume=t.status==2&&!closing&&!storage_failed;s.retry=(t.status==4||t.status==6)&&!closing&&!storage_failed;s.selectFiles=can_select(t)&&!closing&&!storage_failed;
 s.pauseRequested=t.status==1&&t.control.pause;s.dhtNodes=t.control.bt_dht_nodes;s.peers=t.control.bt_peers;s.candidates=t.control.bt_candidates;s.trackerErrors=t.control.bt_tracker_errors;
 if(s.status=="failed"){s.errorCode=t.status==6?"METADATA_TIMEOUT":"NATIVE_ERROR";s.errorMessage=t.error;s.retryable=s.retry;}
 auto&r=s.recovery;r.canResume=s.resume;r.canRetry=s.retry;
 if(t.status==1&&(s.phase==2||(s.phase==0&&t.recheck_required))){r.state="checking";r.message="正在重新校验已有数据；进度可能调整，不能将文件预分配大小视为已完成。";}
 else if(t.status==2){r.state=t.recheck_required?"recheck_required":"paused";r.message=t.recheck_required?"服务重启后已暂停。继续将核验断点与已有文件；当前记录进度尚未重新验证。":"暂停已确认。继续时由内核核验断点；不支持 Range 的 HTTP 源可能从零重下。";}
 else if(s.status=="failed"){r.state="retry_available";r.message="下载已停止，已保留可恢复数据。排除网络或磁盘问题后手动重试；不会自动重试。";}
 else if(t.status==1&&s.phase==6){r.message="正在等待可用对端，当前没有传输；可暂停后稍后继续。";}
 if(t.url.rfind("magnet:?",0)==0){
  if(fs::exists(fs::u8path(t.path)/L"metadata.record")) {
   auto manifest=magnet_files(t.path);std::lock_guard<std::mutex> guard(t.control.file_progress_mutex);
   for(auto const&f:manifest){FileSnapshot v;v.index=f.index;v.name=fs::u8path(f.path).filename().u8string();v.path=f.path;v.size=f.size;v.selected=f.selected;
    if(!f.selected)v.completed=0;
    else if(t.status==3)v.completed=f.size;
    else if(!t.recheck_required&&t.control.file_progress_valid&&size_t(f.index)<t.control.file_progress.size())v.completed=std::min<uint64_t>(f.size,std::max<int64_t>(0,t.control.file_progress[f.index]));
    s.files.push_back(std::move(v));
   }
  }
 }else {FileSnapshot f;f.name=s.name;f.path=t.path;f.size=s.total;if(!t.recheck_required)f.completed=t.bytes;s.files.push_back(std::move(f));}
 return s;
}
std::vector<TaskSnapshot> TaskService::snapshots()const {std::vector<TaskSnapshot> out;for(auto const&t:tasks_)out.push_back(snapshot(*t));return out;}
namespace {
std::string fingerprint(TaskSnapshot const&s){std::ostringstream o;
 o<<s.id<<'|'<<s.status<<'|'<<s.phase<<'|'<<s.bytes<<'|'<<s.total.value_or(0)<<'|'<<int(s.speed)<<'|'<<s.errorCode<<'|'<<s.errorMessage<<'|'<<s.digest<<'|'<<s.pauseRequested<<'|'<<s.selectFiles<<'|'<<s.recovery.state<<'|'<<s.dhtNodes<<'|'<<s.peers<<'|'<<s.candidates<<'|'<<s.trackerErrors;
 for(auto const&f:s.files)o<<'|'<<f.index<<':'<<f.selected<<':'<<(f.completed?std::to_string(*f.completed):"?");return o.str();
}
}
void TaskService::emit(TaskEvent e){e.seq=++sequence_;events_.push_back(std::move(e));while(events_.size()>512)events_.pop_front();}
void TaskService::publish_changes(bool force){auto now=GetTickCount64();if(!force&&now-last_publish_<250)return;last_publish_=now;
 std::set<int> present;
 for(auto const&t:tasks_){present.insert(t->id);auto s=snapshot(*t);auto key=fingerprint(s);auto found=fingerprints_.find(t->id);if(found==fingerprints_.end()||found->second!=key){fingerprints_[t->id]=std::move(key);emit({0,"task.updated",t->id,std::move(s)});}}
 for(auto it=fingerprints_.begin();it!=fingerprints_.end();){if(!present.count(it->first)){emit({0,"task.removed",it->first,std::nullopt});it=fingerprints_.erase(it);}else ++it;}
}
EventBatch TaskService::events(uint64_t after,std::string const&instance)const {
 EventBatch out;out.instanceId=instance_id_;out.latestSeq=sequence_;out.resetRequired=instance!=instance_id_||after>sequence_||(!events_.empty()&&after<events_.front().seq-1);
 if(!out.resetRequired)for(auto const&e:events_)if(e.seq>after)out.events.push_back(e);return out;
}
void TaskService::clear_stopped_tasks(){for(auto const&t:tasks_)if(t->worker.joinable())throw std::runtime_error("cannot clear active workers");tasks_.clear();publish_changes(true);}
void TaskService::request_remove(int id,bool delete_files){auto&t=find(id);t.removing=true;t.delete_files=delete_files;if(t.status==1)t.control.pause=true;}
bool TaskService::remove_stopped(std::function<void(std::vector<fs::path> const&)> recycle){
 bool changed=false;
 for(size_t i=0;i<tasks_.size();){auto&t=*tasks_[i];if(!t.removing||t.status==1){++i;continue;}
  if(t.delete_files){try{auto&inv=t.inventory;auto lock_path=inv.magnet?inv.target/L"magnet.lock":fs::path(inv.target.wstring()+L".hbt-lock");reject_bt_reparse(lock_path);TaskLock lock(lock_path,true);auto plan=inv.plan();recycle(plan);lock.remove_on_release();}
   catch(std::exception const&e){t.removing=false;t.delete_files=false;t.error=e.what();persist();throw;}}
  auto removed=std::move(tasks_[i]);tasks_.erase(tasks_.begin()+i);
  try{save_tasks();}catch(...){removed->removing=false;tasks_.insert(tasks_.begin()+i,std::move(removed));throw;}
  changed=true;
 }
 if(changed)publish_changes(true);return changed;
}

}
