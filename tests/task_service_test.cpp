#include "task_service.hpp"
#include <iostream>
#include <chrono>
namespace fs=std::filesystem;
void require(bool b,char const*s){if(!b)throw std::runtime_error(s);}
int main(){try{
 auto root=fs::current_path()/"test-temp"/("task-service-"+std::to_string(GetCurrentProcessId()));fs::create_directories(root);
 {
  core::TaskService s;s.data_dir=root;
  bool rejected=false;try{s.create(L"",root/L"Downloads");}catch(...){rejected=true;}require(rejected&&s.legacy_tasks().empty(),"empty input mutated queue");
  auto ids=s.create(L"http://127.0.0.1:1/a.bin\nhttp://127.0.0.1:1/b.bin",root/L"Downloads");require(ids.size()==2,"batch creation");
  s.action(ids[0],"pause");s.action(ids[1],"pause");require(s.legacy_tasks()[0]->status==2&&s.legacy_tasks()[1]->status==2,"queued pause");
  s.clear_stopped_tasks();s.load_tasks();require(s.legacy_tasks().size()==2&&s.legacy_tasks()[0]->status==2&&s.next_id==3,"persisted reload");
  auto snap=s.snapshots();require(snap[0].recovery.state=="recheck_required"&&!snap[0].files[0].completed,"restart falsely trusts persisted file progress");
  auto initial=s.latest_seq();auto instance=s.instance_id();
  require(s.events(initial,"wrong-instance").resetRequired,"instance mismatch not reset");
  for(int i=0;i<520;++i){s.action(ids[0],i%2?"pause":"resume");}
  require(s.events(initial,instance).resetRequired,"expired event cursor not reset");
  auto latest=s.latest_seq();require(s.events(latest,instance).events.empty(),"duplicate events after cursor");
  s.publish_changes(true);require(s.latest_seq()==latest,"unchanged snapshot emitted duplicate");
  require(s.events(latest+1,instance).resetRequired,"future cursor not reset");
  s.action(ids[0],"resume");
  auto h=CreateFileW((root/L"tasks.record").c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);require(h!=INVALID_HANDLE_VALUE,"fixture lock");
  rejected=false;try{s.start_next();}catch(...){rejected=true;}CloseHandle(h);
  require(rejected&&s.storage_failed&&s.legacy_tasks()[0]->status==4&&!s.legacy_tasks()[0]->worker.joinable(),"startup persistence failure left phantom worker");
  auto before=GetTickCount64();s.shutdown();require(GetTickCount64()-before<2000,"shutdown hung after startup failure");
 }
 {
  core::TaskService batch;batch.data_dir=root/"partial-batch";fs::create_directories(batch.data_dir);
  auto ids=batch.create(L"http://127.0.0.1:1/first.bin\nhttp://127.0.0.1:1/second.bin",batch.data_dir/L"Downloads");
  batch.action(ids[0],"pause");batch.action(ids[1],"pause");batch.request_remove(ids[0],false);batch.request_remove(ids[1],true);
  bool failed=false;try{batch.remove_stopped([](auto const&){throw std::runtime_error("injected recycler refusal");});}catch(...){failed=true;}
  require(failed&&batch.legacy_tasks().size()==1&&batch.legacy_tasks()[0]->id==ids[1]&&!batch.legacy_tasks()[0]->removing,"partial batch removal left invalid remaining task");
  require(batch.snapshots().size()==1,"snapshot after partial removal");
 }
 fs::remove_all(root);std::cout<<"TaskService queue/reload and startup failure shutdown: PASS\n";return 0;
}catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
