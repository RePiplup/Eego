#include "task_service.hpp"
#include "file_store.hpp"
#include "checkpoint.hpp"
#include <iostream>
#include <future>
#include <fstream>
using namespace std::chrono_literals;
namespace fs=std::filesystem;
void need(bool value,char const*msg){if(!value)throw std::runtime_error(msg);}
int main(){try{
 auto root=fs::current_path()/"test-temp"/("concurrency-"+std::to_string(GetCurrentProcessId()));fs::create_directories(root);
 core::TaskService service;service.data_dir=root;service.load_tasks();need(service.max_active_tasks()==1,"default");
 service.set_max_active_tasks(3);core::TaskService reopened;reopened.data_dir=root;reopened.load_tasks();need(reopened.max_active_tasks()==3,"reload");
 for(int n:{0,-1,9,16}){bool failed=false;try{service.set_max_active_tasks(n);}catch(...){failed=true;}need(failed&&service.max_active_tasks()==3,"invalid setting mutated");}
 auto h=CreateFileW((root/L"settings.record").c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);need(h!=INVALID_HANDLE_VALUE,"setting lock");
 bool failed=false;try{service.set_max_active_tasks(2);}catch(...){failed=true;}CloseHandle(h);need(failed&&service.max_active_tasks()==3&&!service.storage_failed,"settings failure changed live state");
 reopened.load_settings();need(reopened.max_active_tasks()==3,"settings failure damaged old record");
 core::save_record(root/L"settings.record","SETTINGS1 16\n");
 reopened.load_settings();need(reopened.max_active_tasks()==8&&reopened.settings_error().empty(),"legacy limit capped at eight");
 service.set_max_active_tasks(8);reopened.load_settings();need(reopened.max_active_tasks()==8,"eight persists");
 {std::ofstream bad(root/L"settings.record",std::ios::trunc);bad<<"corrupt";}
 reopened.load_settings();need(reopened.max_active_tasks()==1&&!reopened.settings_error().empty(),"corrupt fallback");need(fs::file_size(root/L"settings.record")==7,"corrupt file overwritten");
 service.set_max_active_tasks(2);need(service.settings_error().empty(),"repair warning");
 std::atomic<int> a{0},b{0};auto pa=root/"a",pb=root/"ab";
 auto ta=core::FileStore::observe([=](auto const&p){return p==pa;},[&](auto const&,auto const&){++a;});
 auto tb=core::FileStore::observe([=](auto const&p){return p==pb;},[&](auto const&,auto const&){++b;});
 std::thread x([&]{for(int i=0;i<500;++i)core::FileStore::notify_created(pa,{});});
 std::thread y([&]{for(int i=0;i<500;++i)core::FileStore::notify_created(pb,{});});x.join();y.join();need(a==500&&b==500,"observer cross route");
 core::FileStore::unobserve(ta);core::FileStore::notify_created(pa,{});core::FileStore::notify_created(pb,{});need(a==500&&b==501,"unsubscribe cleared another observer");core::FileStore::unobserve(tb);
 std::promise<void> entered,release;auto gate=release.get_future().share();
 ta=core::FileStore::observe([=](auto const&p){return p==pa;},[&](auto const&,auto const&){entered.set_value();gate.wait();});
 auto notification=std::async(std::launch::async,[&]{core::FileStore::notify_created(pa,{});});entered.get_future().wait();
 auto unregister=std::async(std::launch::async,[&]{core::FileStore::unobserve(ta);});need(unregister.wait_for(100ms)==std::future_status::timeout,"unregister did not await callback");release.set_value();notification.get();unregister.get();core::FileStore::notify_created(pa,{});
 std::cout<<"PASS default/reload/invalid/write-failure/corrupt-settings; parallel observer routing, unsubscribe isolation and in-flight lifetime\n";
 return 0;
}catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
