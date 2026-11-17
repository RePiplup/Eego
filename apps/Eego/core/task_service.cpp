#include "task_service.hpp"
#include "file_store.hpp"
#include "output_layout.hpp"
#include "browser_bridge.hpp"
#include <libtorrent/hasher.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <set>
namespace core {
namespace fs=std::filesystem;
using Task=ServiceTask;
constexpr int max_concurrent_downloads = 8;
std::string TaskService::file_hash(Task const &t) {
  if(t.url.rfind("magnet:?",0)==0)return core::verify_magnet_output(t.path,t.magnet_state);
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
  if(t.inventory.magnet){
    t.magnet_state=data_dir/L"magnet-state"/std::to_wstring(t.id);
    auto saved=output_name(t.magnet_state);
    if(!saved.leaf.empty())t.inventory.output=t.inventory.target.parent_path()/saved.leaf;
    t.inventory.single_output=saved.single;
  }
  t.inventory.load();
}
namespace {
fs::path physical_file(Task const& t,fs::path const& relative) {
  auto const& inv=t.inventory;
  if(inv.output.empty())return inv.target/L"files"/relative;
  if(inv.single_output)return inv.output;
  return inv.output.parent_path()/named_relative(relative,inv.output.filename());
}

void remove_empty_dir(fs::path const& dir) {
  reject_bt_reparse(dir);
  if(fs::is_directory(dir)&&fs::is_empty(dir))fs::remove(dir);
}

// Claim a complete top-level name with a same-volume, no-replace rename.
// The durable intent and old file IDs allow replay on either side of the rename.
void prepare_output(Task& t,bool starting=false) {
  auto& inv=t.inventory;
  if(!inv.magnet || t.magnet_state==inv.target)return;
  auto state=t.magnet_state;
  auto old=inv.target/L"files";
  reject_bt_reparse(old);
  bool finished=fs::exists(state/L"files.record");
  if(!finished&&!fs::exists(state/L"selection.record")&&!fs::exists(old)){
    if(fs::exists(state/L"magnet.owner")&&inv.output.empty())remove_empty_dir(inv.target);
    return;
  }
  TaskLock lock(state/L"magnet.lock",true);
  auto manifest=magnet_files(t.path,state);
  if(manifest.empty())throw std::runtime_error("磁力文件清单为空");
  auto first=fs::u8path(manifest.front().path);
  if(first.empty())throw std::runtime_error("资源名为空");
  auto top=*first.begin();
  lt::file_storage check;
  for(auto const& f:manifest){
    auto path=fs::u8path(f.path);
    if(path.empty()||path.has_root_path()||*path.begin()!=top)throw std::runtime_error("资源包含无效路径");
    for(auto const& part:path)if(part==L"."||part==L".."||part.empty())throw std::runtime_error("资源包含无效路径");
    check.add_file(f.path,f.size);
  }
  validate_bt_files(check);
  std::set<fs::path> selected;
  for(auto const& f:manifest)if(f.selected)selected.insert(fs::u8path(f.path));
  if(selected.empty())throw std::runtime_error("没有选中的下载文件");
  bool single=selected.size()==1;
  auto source=old/(single?*selected.begin():top);
  bool moving=inv.output.empty() || fs::exists(source);
  bool pending=false;
  for(auto const& file:manifest)if(inv.entries.count(old/fs::u8path(file.path)))pending=true;
  if(!moving&&!starting&&!pending){lock.remove_on_release();return;}
  if(moving && inv.output.empty() && !fs::exists(source) && !starting){
    if(finished)throw std::runtime_error("已完成资源缺失");
    lock.remove_on_release();return;
  }
  // Never move an unrelated file that was added to an old torrent folder.
  if(moving && fs::is_directory(source))for(auto const& e:fs::recursive_directory_iterator(source)){
    reject_bt_reparse(e.path());
    if(!e.is_directory()&&!selected.count(e.path().lexically_relative(old)))throw std::runtime_error("原目录含有任务以外的文件，已保留原目录");
  }
  for(auto const& f:manifest)if(f.selected){
    auto relative=fs::u8path(f.path);
    auto prior=old/relative;
    auto file=moving?prior:physical_file(t,relative);
    reject_bt_reparse(file);
    if(!fs::exists(file)){
      if(finished||!inv.output.empty()||inv.entries.count(prior))throw std::runtime_error("任务数据缺失，原记录已保留");
      FileStore::prepare(file,f.size,false);
      inv.event(file,{});
    } else {
      auto entry=inv.entries.find(file);
      if(entry==inv.entries.end())entry=inv.entries.find(prior);
      auto id=managed::identity(file,false);
      if(entry==inv.entries.end()||(entry->second!=id&&entry->second.rfind(id+':',0)!=0)||fs::file_size(file)!=f.size)
        throw std::runtime_error("下载文件身份或大小不符，已保留原文件");
      if((moving||pending) && entry->second!=id && entry->second!=managed::identity(file))
        throw std::runtime_error("文件发生修改，停止迁移");
    }
  }
  if(inv.output.empty()){
    inv.save();
    auto parent=inv.target.parent_path();auto original=source.filename();auto leaf=original;
    for(int suffix=2;fs::exists(parent/leaf)||parent/leaf==inv.target;++suffix){
      auto tail=L" ("+std::to_wstring(suffix)+L")";
      leaf=single?fs::path(original.stem().wstring()+tail+original.extension().wstring()):fs::path(original.wstring()+tail);
    }
    save_record(state/L"output.record",(single?"FILE\n":"TREE\n")+leaf.u8string());
    inv.output=parent/leaf;inv.single_output=single;
  }
  if(moving){
    reject_bt_reparse(inv.output);
    if(!MoveFileExW(source.c_str(),inv.output.c_str(),MOVEFILE_WRITE_THROUGH))
      throw std::runtime_error("无法移动到资源名位置，请检查同名文件或文件占用");
  }
  bool changed=false;
  for(auto const& relative:selected){
    auto it=inv.entries.find(old/relative);
    if(it!=inv.entries.end()){
      auto path=physical_file(t,relative);
      auto id=managed::identity(path,false);
      if(it->second!=id&&it->second.rfind(id+':',0)!=0)throw std::runtime_error("移动后的文件身份不符");
      inv.entries[path]=it->second;inv.entries.erase(it);changed=true;
    }
  }
  if(changed)inv.save();
  for(auto parent=source.parent_path();parent!=old;parent=parent.parent_path())remove_empty_dir(parent);
  remove_empty_dir(old);remove_empty_dir(inv.target);lock.remove_on_release();
}

void prepare_existing_layout(Task& t,fs::path const& library) {
  auto target=t.inventory.target;
  auto state=t.magnet_state;
  reject_bt_reparse(state);
  if(!fs::exists(target/L"magnet.owner") && !fs::exists(target/L"magnet.lock"))return;
  // Libraries inherited from HandmadeBT can still share these external tasks.
  if(target.parent_path()!=library/L"Downloads"){
    t.magnet_state=target;
    return;
  }
  reject_bt_reparse(target/L"magnet.lock");
  TaskLock legacy(target/L"magnet.lock",true);
  fs::create_directories(state);
  TaskLock current(state/L"magnet.lock",true);
  auto info=lt::parse_magnet_uri(t.url);
  std::ostringstream owner;owner<<info.info_hashes.v1<<'\n'<<target.u8string();
  auto binding=fs::exists(target/L"magnet.owner")?target/L"magnet.owner":state/L"magnet.owner";
  reject_bt_reparse(binding);
  if(load_record(binding)!=owner.str())throw std::runtime_error("magnet owner mismatch");
  auto check_owned=[&](fs::path const& path){
    auto it=t.inventory.entries.find(path);
    if(it==t.inventory.entries.end() || it->second!=managed::identity(path))
      throw std::runtime_error("legacy state ownership could not be verified");
  };
  // Only migrate inventoried records. A user file in the cache directory
  // must never become eligible for automatic cleanup merely by its name.
  for(auto name:{L"metadata.record",L"selection.record",L"dht.record",L"files.record",L"boundary-cache"}){
    auto path=target/name;
    if(!fs::exists(path))continue;
    reject_bt_reparse(path);
    if(path.filename()==L"boundary-cache"){
      for(auto const& entry:fs::directory_iterator(path))check_owned(entry.path());
    } else check_owned(path);
  }
  // Same-volume rename preserves completion receipt identities. Owner moves last.
  for(auto name:{L"metadata.record",L"selection.record",L"dht.record",L"files.record",L"boundary-cache",L"magnet.owner"}){
    auto from=target/name,to=state/name;
    if(!fs::exists(from))continue;
    reject_bt_reparse(from);reject_bt_reparse(to);
    if(fs::exists(to))throw std::runtime_error("migration destination exists");
    fs::rename(from,to);
  }
  legacy.remove_on_release();
  current.remove_on_release();
}
void clean_completed(Task const& t) {
  auto state=t.magnet_state;
  if(t.status!=3 || state.empty() || state==t.inventory.target || !fs::exists(state/L"files.record"))return;
  reject_bt_reparse(state/L"magnet.lock");
  TaskLock lock(state/L"magnet.lock",true);
  auto boundary=state/L"boundary-cache";
  reject_bt_reparse(boundary);
  if(fs::exists(boundary)){
    for(auto const& e:fs::directory_iterator(boundary)){
      auto stem=e.path().stem().wstring();
      if(e.path().extension()!=L".part" || stem.empty() || stem.find_first_not_of(L"0123456789")!=stem.npos)continue;
      reject_bt_reparse(e.path());if(e.is_regular_file())fs::remove(e.path());
    }
    if(fs::is_empty(boundary))fs::remove(boundary);
  }
  for(auto name:{L"dht.record",L"metadata.record",L"selection.record",L"magnet.owner"}){
    auto path=state/name;reject_bt_reparse(path);fs::remove(path);
  }
  lock.remove_on_release();
}
bool overlapping(Task const& a,Task const& b) {
  auto ap=fs::weakly_canonical(fs::u8path(a.path)).make_preferred().wstring();
  auto bp=fs::weakly_canonical(fs::u8path(b.path)).make_preferred().wstring();
  auto inside=[](std::wstring const& child,std::wstring const& parent){
    return child.size()>=parent.size()&&CompareStringOrdinal(child.data(),int(parent.size()),parent.data(),int(parent.size()),TRUE)==CSTR_EQUAL
      &&(child.size()==parent.size()||child[parent.size()]==L'\\');
  };
  return (a.inventory.magnet&&inside(bp,ap))||(b.inventory.magnet&&inside(ap,bp))||
    CompareStringOrdinal(ap.c_str(),-1,bp.c_str(),-1,TRUE)==CSTR_EQUAL;
}
// Workers can publish concurrently. Replace one checksummed ledger atomically.
std::mutex receipts_mutex;
std::string receipt(Task const& t,std::optional<std::string> update={}) {
  std::lock_guard<std::mutex> guard(receipts_mutex);
  auto dir=t.inventory.record.parent_path();
  auto ledger=dir/L"completion.record";
  auto legacy=dir/("completion-"+std::to_string(t.id)+".record");
  std::map<int,std::string> records;
  if(fs::exists(ledger)){
    std::istringstream in(load_record(ledger));std::string tag;int id;
    if(!(in>>tag)||tag!="RECEIPTS1")throw std::runtime_error("invalid completion ledger");
    while(!(in>>std::ws).eof()){
      std::string value;
      if(!(in>>id>>std::quoted(value))||id<=0||!records.emplace(id,value).second)
        throw std::runtime_error("invalid completion ledger entry");
    }
  }
  bool old=fs::exists(legacy);
  if(!update&&old&&!records.count(t.id))update=load_record(legacy);
  if(update){
    if(update->empty())records.erase(t.id);else records[t.id]=*update;
    std::ostringstream out;out<<"RECEIPTS1\n";
    for(auto const& [id,value]:records)out<<id<<' '<<std::quoted(value)<<'\n';
    save_record(ledger,out.str());
  }
  if(old)fs::remove(legacy);
  auto it=records.find(t.id);return it==records.end()?std::string{}:it->second;
}
bool owned(Task const& t, fs::path const& p) {
  auto it=t.inventory.entries.find(p);
  return it!=t.inventory.entries.end() && it->second==managed::identity(p);
}
void verify_completed_on_load(Task const& t) {
  if(t.digest.size()!=64 || (!t.expected.empty() && t.expected!=t.digest))
    throw std::runtime_error("completed file digest missing or inconsistent");
  WIN32_FILE_ATTRIBUTE_DATA record{};
  GetFileAttributesExW(t.inventory.record.c_str(),GetFileExInfoStandard,&record);
  ULARGE_INTEGER saved{};
  saved.HighPart=record.ftLastWriteTime.dwHighDateTime;
  saved.LowPart=record.ftLastWriteTime.dwLowDateTime;
  auto unchanged=[&](fs::path const& file){
    auto entry=t.inventory.entries.find(file);
    if(entry==t.inventory.entries.end() || !saved.QuadPart)return false;
    // The inventory saved after completion records file ID, size and last write.
    // Also check Windows change time in case another program restored last write.
    LONGLONG changed=0;
    try {
      return managed::identity(file,true,&changed)==entry->second &&
             changed>0 && static_cast<ULONGLONG>(changed)<saved.QuadPart;
    } catch(std::exception const&) { return false; }
  };
  if(!t.inventory.magnet && fs::file_size(t.inventory.target)==t.total && unchanged(t.inventory.target))return;
  auto digest=t.inventory.magnet?verify_magnet_output(t.path,t.magnet_state,unchanged):TaskService::file_hash(t);
  if(digest!=t.digest)throw std::runtime_error("completed file hash changed");
}
// The HTTP file ID survives the final rename. For BT the receipt binds the
// verified file manifest to this task and its verified torrent metadata.
bool recover_published(Task& t) {
  bool bt=t.inventory.magnet;
  auto root=t.inventory.target;
  auto final=bt ? t.magnet_state/L"files.record" : root;
  if(!fs::exists(final))return false;
  auto anchor=bt ? t.magnet_state/L"metadata.record" : final;
  std::string wanted=t.expected;
  auto proof=receipt(t);
  if(!proof.empty()) {
    std::istringstream in(proof);
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
    if(load_record(t.magnet_state/L"magnet.owner")!=owner.str())
      throw std::runtime_error("磁力任务与文件归属不符，原文件已保留");
    t.total=0;
    for(auto const& file:magnet_files(t.path,t.magnet_state))if(file.selected) {
      if(!owned(t,physical_file(t,fs::u8path(file.path))))
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
void TaskService::load_settings() {
  max_active_tasks_=1;settings_error_.clear();
  auto path=data_dir/L"settings.record";
  try {
    if(!fs::exists(path))return;
    std::istringstream in(load_record(path));std::string magic;int value=0;
    if(!(in>>magic>>value)||magic!="SETTINGS1"||value<1||value>16)throw std::runtime_error("invalid concurrency settings");
    in>>std::ws;if(!in.eof())throw std::runtime_error("trailing settings data");
    // Earlier releases saved valid limits up to 16.
    max_active_tasks_=std::min(value, max_concurrent_downloads);
  }catch(std::exception const&e){settings_error_=std::string("Settings unreadable; using 1. Original preserved: ")+e.what();}
}
void TaskService::set_max_active_tasks(int value) {
  if(closing||storage_failed)throw std::runtime_error("task service unavailable");
  if(value<1||value>max_concurrent_downloads)throw std::runtime_error("maxActiveTasks must be an integer from 1 to 8");
  // Commit on disk before exposing the value or starting additional workers.
  save_record(data_dir/L"settings.record","SETTINGS1 "+std::to_string(value)+"\n");
  max_active_tasks_=value;settings_error_.clear();
}
void TaskService::load_tasks() {
  load_settings();
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
    init_inventory(*t);
    if(fs::exists(data_dir/("completion-"+std::to_string(t->id)+".record")))receipt(*t);
    if(t->inventory.magnet)prepare_existing_layout(*t,data_dir);
    if(t->status==0||t->status==1) { t->status=2; t->error="重开后已暂停，点击恢复继续"; }
    try {
      if(t->status==3 && t->inventory.magnet && t->inventory.output.empty() && t->magnet_state!=t->inventory.target)
        verify_completed_on_load(*t);
      prepare_output(*t);
    }
    catch(std::exception const& e){t->status=4;t->error=e.what();}
    if(t->status==3) {
      try {verify_completed_on_load(*t);}
      catch(std::exception const &e){t->status=4;t->error=e.what();}
    }
    if(t->url.rfind("magnet:?",0)==0&&fs::exists(t->magnet_state/L"metadata.record")) {
      try {uint64_t selected_total=0;for(auto const&f:magnet_files(t->path,t->magnet_state))if(f.selected)selected_total+=f.size;t->total=selected_total;}
      catch(std::exception const&e){t->status=4;t->error=e.what();}
    }
    for(auto const&existing:tasks_)if(overlapping(*t,*existing))throw std::runtime_error("overlapping task destinations; original data preserved");
    if(t->status==2 || (t->status==4 && t->error=="output exists; refusing overwrite")) {
      try { if(recover_published(*t))reconciled=true; }
      catch(std::exception const& e){t->status=4;t->error=e.what();reconciled=true;}
    }
    t->recheck_required=t->status==2;
    tasks_.push_back(std::move(t));
  }
  in>>std::ws;if(!in.eof())throw std::runtime_error("trailing task-list data");
  if(reconciled)save_tasks();
  for(auto const& t:tasks_)clean_completed(*t);
}
void TaskService::persist() {
  try { save_tasks(); } catch(...) { storage_failed=true; for(auto &t:tasks_)if(t->status==1)t->control.pause=true; throw; }
}
void TaskService::start_next() {
  if(closing||storage_failed)return;
  int active=0;for(auto const &t:tasks_)if(t->status==1)++active;
  if(active>=max_active_tasks_)return;
  for(auto &p:tasks_)if(p->status==0&&!p->removing) {
    auto t=p.get();
    try {
    prepare_output(*t,true);
    try { if(recover_published(*t)){persist();clean_completed(*t);continue;} }
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
    t->observer=FileStore::observe([t](auto const&p){return t->inventory.accepts(p);},[t](auto const&p,auto const&from){t->inventory.event(p,from);});
    t->control.before_publish=[t](fs::path const& file,std::string const& digest){
      t->inventory.save();
      std::ostringstream out;
      out<<std::quoted(t->inventory.binding)<<' '<<std::quoted(managed::identity(file))<<' '<<digest;
      receipt(*t,out.str());
    };
    t->worker=std::thread([t] {
      try {
        if(t->url.rfind("magnet:?",0)==0)t->result=core::magnet_download(t->url,t->path,t->control,true,t->magnet_state);
        else t->result=core::http_download(t->url,t->path,4,0,nullptr,&t->control);
      }
      catch(core::BrowserAuthorizationError const &e){t->worker_error=e.what();t->browser_authorization_needed=true;t->result=1;}
      catch(std::exception const &e){t->worker_error=e.what();t->result=1;}
      t->ended.store(true,std::memory_order_release);
    });
    if(++active>=max_active_tasks_)return;
    }catch(std::exception const&e){
      FileStore::unobserve(t->observer);t->observer=0;t->status=4;t->error=e.what();t->worker_error.clear();t->ended=false;
      if(!storage_failed){try{persist();}catch(...){}}
      if(storage_failed)throw;
    }
  }
}
bool TaskService::poll() {
  bool changed=false,active=false;
  std::vector<Task*> completed;
  for(auto &p:tasks_)if(p->status==1) {
    auto &t=*p;
    if(t.ended.load(std::memory_order_acquire)) {
      if(t.worker.joinable())t.worker.join();core::FileStore::unobserve(t.observer);t.observer=0;t.bytes=t.control.received;t.total=t.control.total;
      t.error=t.worker_error;
      t.status=t.result==0?3:(t.result==3?2:(t.result==4?5:(t.result==5?6:4)));
      if(t.status==6)t.error="5 分钟内未找到可提供元数据的节点；可能是节点离线或网络连接受限，不代表资源不存在。可稍后重试。";
      t.digest=t.control.result_sha256;
      if(t.status==3)completed.push_back(&t);
      if(t.status!=2)t.control.browser_session={}; // Drop credentials after completion/failure.
      try{t.inventory.save();}catch(std::exception const&e){t.error+=std::string("；归属清单保存失败，文件删除将受限：")+e.what();}
      changed=true;
    } else if(t.worker.joinable())active=true;
    else {t.status=4;t.error="worker did not start";changed=true;}
  }
  if(changed&&!storage_failed){persist();for(auto t:completed)clean_completed(*t);}
  sample();
  publish_changes();
  return active;
}
std::wstring trim(std::wstring s) {
  auto a=s.find_first_not_of(L" \t\r\n");if(a==s.npos)return {};
  return s.substr(a,s.find_last_not_of(L" \t\r\n")-a+1);
}
namespace {
std::wstring decode_url_leaf(std::wstring const& leaf) {
  auto bytes=browser_bridge::utf8(leaf);
  std::string decoded;
  auto hex=[](char c)->int {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
  };
  for(size_t i=0;i<bytes.size();++i){
    if(bytes[i]=='%'&&i+2<bytes.size()){
      int a=hex(bytes[i+1]),b=hex(bytes[i+2]);
      if(a>=0&&b>=0){decoded.push_back(char(16*a+b));i+=2;continue;}
    }
    decoded.push_back(bytes[i]);
  }
  try{return browser_bridge::wide(decoded);}catch(std::runtime_error const&){return leaf;}
}
fs::path http_leaf(std::wstring const& url,std::wstring const& suggested) {
  auto clean=url.substr(0,url.find_first_of(L"?#"));
  auto start=clean.find(L'/',clean.find(L"://")+3);
  auto leaf=start==clean.npos?std::wstring{}:clean.substr(clean.rfind(L'/')+1);
  auto name=fs::path(suggested).filename().wstring();
  if((name.empty()||name==leaf)&&!leaf.empty())name=decode_url_leaf(leaf);
  for(auto& c:name)if(c<32||std::wstring_view(L"<>:\"/\\|?*").find(c)!=std::wstring_view::npos)c=L'_';
  name.erase(name.find_last_not_of(L". ")+1);
  if(name.empty())return L"download.bin";
  if(name.size()>180){
    auto ext=fs::path(name).extension().wstring();
    if(ext.size()>32)ext.clear();
    name.resize(180-ext.size());
    if(name.back()>=0xd800&&name.back()<=0xdbff)name.pop_back();
    name+=ext;
  }
  auto stem=name.substr(0,name.find(L'.'));
  stem.erase(stem.find_last_not_of(L' ')+1);
  CharUpperBuffW(stem.data(),DWORD(stem.size()));
  if(stem==L"CON"||stem==L"PRN"||stem==L"AUX"||stem==L"NUL"||stem==L"CONIN$"||stem==L"CONOUT$"||
     (stem.size()==4&&(stem.substr(0,3)==L"COM"||stem.substr(0,3)==L"LPT")&&
      std::wstring_view(L"0123456789¹²³").find(stem[3])!=std::wstring_view::npos))name.insert(0,1,L'_');
  return name;
}
struct PathLess {
  bool operator()(std::wstring const& a,std::wstring const& b) const {
    return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;
  }
};
const std::wstring_view http_suffixes[]={L"",L".part",L".resume",L".hbt-lock"};
}
std::vector<int> TaskService::create(std::wstring const &external,fs::path destination,std::wstring const &suggested) {
  if(closing||storage_failed)throw std::runtime_error("task service unavailable");
  auto dir=fs::absolute(destination).lexically_normal();
  fs::create_directories(dir);
  std::wistringstream input(external);std::wstring line;
  std::vector<std::unique_ptr<Task>> pending;
  std::set<std::wstring,PathLess> occupied;
  auto reserve=[&](Task const& t){
    auto base=t.inventory.target.wstring();
    occupied.insert(base);
    if(t.inventory.magnet){
      if(!t.inventory.output.empty())occupied.insert(t.inventory.output.wstring());
    }else for(auto suffix:http_suffixes)occupied.insert(std::wstring(base).append(suffix));
  };
  for(auto const& t:tasks_)reserve(*t);
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
    auto target=dir/(std::to_wstring(t->id)+L"-magnet");
    if(!magnet){
      auto original=http_leaf(url,suggested);
      target=dir/original;
      auto unavailable=[&](std::wstring_view suffix){
        auto file=target.wstring().append(suffix);
        return occupied.count(file)||fs::exists(fs::symlink_status(file));
      };
      for(unsigned n=2;std::any_of(std::begin(http_suffixes),std::end(http_suffixes),unavailable);++n)
        target=dir/(original.stem().wstring()+L" ("+std::to_wstring(n)+L")"+original.extension().wstring());
    }
    t->path=target.u8string();init_inventory(*t);
    reserve(*t);
    if(magnet && fs::exists(t->inventory.target))throw std::runtime_error("magnet destination already exists");
    for(auto const&existing:tasks_)if(overlapping(*t,*existing))throw std::runtime_error("destination overlaps another task root");
    for(auto const&existing:pending)if(overlapping(*t,*existing))throw std::runtime_error("batch destinations overlap");
    pending.push_back(std::move(t));
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
  auto path=t.magnet_state/L"selection.record";bool fresh=!fs::exists(path);
  core::select_magnet_files(t.path,indexes,t.magnet_state);
  if(fresh)t.inventory.event(path,{});
  t.total=0;for(auto const&f:magnet_files(t.path,t.magnet_state))if(f.selected)t.total+=f.size;t.bytes=0;
  t.inventory.save();t.status=0;t.error.clear();persist();publish_changes(true);
}

std::vector<MagnetFile> TaskService::files(ServiceTask const&t){return magnet_files(t.path,t.magnet_state);}
bool TaskService::can_select(ServiceTask const&t){return t.url.rfind("magnet:?",0)==0&&t.status!=1&&!t.removing&&t.inventory.output.empty()&&fs::exists(t.magnet_state/L"metadata.record")&&!fs::exists(fs::u8path(t.path)/L"files");}
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
  for(auto&t:tasks_){FileStore::unobserve(t->observer);t->observer=0;}
}
TaskService::~TaskService(){shutdown();}
TaskService::TaskService(){FILETIME ft;GetSystemTimeAsFileTime(&ft);instance_id_=std::to_string(GetCurrentProcessId())+"-"+std::to_string((uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime);}
TaskSnapshot TaskService::snapshot(ServiceTask const&t)const {
 TaskSnapshot s;s.id=t.id;s.name=fs::u8path(t.path).filename().u8string();s.url=t.url;s.path=t.path;s.phase=t.control.phase;
 s.status=t.removing?"removing":t.status==0?"queued":t.status==2?"paused":t.status==3?"completed":t.status==4||t.status==6?"failed":t.status==5?"choosing":s.phase==1?"downloading":"fetching";
 s.bytes=t.bytes;if(t.total)s.total=t.total;s.speed=t.speed;s.digest=t.digest;
 s.pause=(t.status==0||t.status==1)&&!closing&&!storage_failed;s.resume=t.status==2&&!closing&&!storage_failed;s.retry=(t.status==4||t.status==6)&&!closing&&!storage_failed;s.selectFiles=can_select(t)&&!closing&&!storage_failed;
 s.remove=!t.removing&&!closing&&!storage_failed;
 if(t.removing)s.pause=s.resume=s.retry=s.selectFiles=false;
 s.pauseRequested=t.status==1&&t.control.pause;s.dhtNodes=t.control.bt_dht_nodes;s.peers=t.control.bt_peers;s.candidates=t.control.bt_candidates;s.trackerErrors=t.control.bt_tracker_errors;
 if(s.status=="failed"){s.errorCode=t.status==6?"METADATA_TIMEOUT":"NATIVE_ERROR";s.errorMessage=t.error;s.retryable=s.retry;}
 if(s.errorMessage=="server returned an HTML page, not a downloadable file; check the link in browser")
   s.errorMessage="此地址返回了网页。请在原网页点击文件下载按钮，或使用文件的直接下载地址。";
 auto&r=s.recovery;r.canResume=s.resume;r.canRetry=s.retry;
 if(t.status==1&&(s.phase==2||(s.phase==0&&t.recheck_required))){r.state="checking";r.message="正在重新校验已有数据；进度可能调整，不能将文件预分配大小视为已完成。";}
 else if(t.status==2){r.state=t.recheck_required?"recheck_required":"paused";r.message=t.recheck_required?"服务重启后已暂停。继续将核验断点与已有文件；当前记录进度尚未重新验证。":"暂停已确认。继续时由内核核验断点；不支持 Range 的 HTTP 源可能从零重下。";}
 else if(s.status=="failed"){r.state="retry_available";r.message="下载已停止，已保留可恢复数据。排除网络或磁盘问题后手动重试；不会自动重试。";}
 else if(t.status==1&&s.phase==6){r.message="正在等待可用对端，当前没有传输；可暂停后稍后继续。";}
 if(t.url.rfind("magnet:?",0)==0){
  s.path=t.inventory.output.empty()?(t.magnet_state==t.inventory.target?t.inventory.target:t.inventory.target.parent_path()).u8string():t.inventory.output.u8string();
  s.name="磁力任务 #"+std::to_string(t.id);
  if(fs::exists(t.magnet_state/L"metadata.record")||fs::exists(t.magnet_state/L"files.record")) {
   auto manifest=magnet_files(t.path,t.magnet_state);
   const MagnetFile* chosen=nullptr;
   size_t selected_count=0;
   for(const auto& file:manifest) {
    if(file.selected){chosen=&file;++selected_count;}
   }
   if(selected_count==1) s.name=fs::u8path(chosen->path).filename().u8string();
   else if(!manifest.empty()) {
    // libtorrent prefixes every multi-file path with the torrent root name.
    auto path=fs::u8path(manifest.front().path);
    s.name=path.begin()->u8string();
   }
   std::lock_guard<std::mutex> guard(t.control.file_progress_mutex);
   for(auto const&f:manifest){FileSnapshot v;v.index=f.index;v.name=fs::u8path(f.path).filename().u8string();v.path=f.path;v.diskPath=physical_file(t,fs::u8path(f.path)).u8string();v.size=f.size;v.selected=f.selected;
    if(!f.selected)v.completed=0;
    else if(t.status==3)v.completed=f.size;
    else if(!t.recheck_required&&t.control.file_progress_valid&&size_t(f.index)<t.control.file_progress.size())v.completed=std::min<uint64_t>(f.size,std::max<int64_t>(0,t.control.file_progress[f.index]));
    s.files.push_back(std::move(v));
   }
  } else {
   lt::error_code ec;
   auto params=lt::parse_magnet_uri(t.url,ec);
   if(!ec && !params.name.empty())s.name=params.name;
  }
 }else {FileSnapshot f;f.name=s.name;f.path=t.path;f.size=s.total;if(!t.recheck_required)f.completed=t.bytes;s.files.push_back(std::move(f));}
 return s;
}
std::vector<TaskSnapshot> TaskService::snapshots()const {std::vector<TaskSnapshot> out;for(auto const&t:tasks_)out.push_back(snapshot(*t));return out;}
namespace {
std::string fingerprint(TaskSnapshot const&s){std::ostringstream o;
 o<<s.id<<'|'<<s.name<<'|'<<s.status<<'|'<<s.phase<<'|'<<s.bytes<<'|'<<s.total.value_or(0)<<'|'<<int(s.speed)<<'|'<<s.errorCode<<'|'<<s.errorMessage<<'|'<<s.digest<<'|'<<s.pauseRequested<<'|'<<s.selectFiles<<'|'<<s.recovery.state<<'|'<<s.dhtNodes<<'|'<<s.peers<<'|'<<s.candidates<<'|'<<s.trackerErrors;
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
void TaskService::request_remove(int id,bool delete_files){if(closing||storage_failed)throw std::runtime_error("task service unavailable");auto&t=find(id);if(t.removing)throw std::runtime_error("task being removed");t.removing=true;t.delete_files=delete_files;if(t.status==1)t.control.pause=true;}
bool TaskService::remove_stopped(std::function<void(std::vector<fs::path> const&)> recycle){
 bool changed=false;
 for(size_t i=0;i<tasks_.size();){auto&t=*tasks_[i];if(!t.removing||t.status==1){++i;continue;}
  if(t.delete_files){try{auto&inv=t.inventory;auto lock_path=inv.magnet?t.magnet_state/L"magnet.lock":fs::path(inv.target.wstring()+L".hbt-lock");reject_bt_reparse(lock_path);TaskLock lock(lock_path,true);auto plan=inv.plan();recycle(plan);lock.remove_on_release();}
   catch(std::exception const&e){t.removing=false;t.delete_files=false;t.error=e.what();persist();throw;}}
  auto removed=std::move(tasks_[i]);tasks_.erase(tasks_.begin()+i);
  try{save_tasks();}catch(...){removed->removing=false;tasks_.insert(tasks_.begin()+i,std::move(removed));throw;}
  if(!receipt(*removed).empty())receipt(*removed,std::string{});
  changed=true;
 }
 if(changed)publish_changes(true);return changed;
}

}
