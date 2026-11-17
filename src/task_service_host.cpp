#include "task_service.hpp"
#include "browser_bridge.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include "bt_files.hpp"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <fstream>
namespace fs=std::filesystem;
using core::ServiceTask;
std::string quote(std::string const&s){std::ostringstream o;o<<'"';for(unsigned char c:s){switch(c){case '"':o<<"\\\"";break;case '\\':o<<"\\\\";break;case '\n':o<<"\\n";break;case '\r':o<<"\\r";break;case '\t':o<<"\\t";break;default:if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else o<<char(c);}}o<<'"';return o.str();}
std::string number(std::optional<uint64_t> const&v){return v?std::to_string(*v):"null";}
char const* json_bool(bool b){return b?"true":"false";}
std::string files_json(std::vector<core::FileSnapshot> const&files){std::ostringstream o;o<<'[';bool first=true;for(auto const&f:files){if(!first)o<<',';first=false;o<<"{\"index\":"<<f.index<<",\"name\":"<<quote(f.name)<<",\"path\":"<<quote(f.path)<<",\"size\":"<<number(f.size)<<",\"completed\":"<<number(f.completed)<<",\"selected\":"<<json_bool(f.selected)<<'}';}o<<']';return o.str();}
std::string snapshot(core::TaskSnapshot const&t){
 std::ostringstream o;o<<"{\"id\":"<<t.id<<",\"name\":"<<quote(t.name)<<",\"url\":"<<quote(t.url)<<",\"path\":"<<quote(t.path)<<",\"status\":"<<quote(t.status)<<",\"phase\":"<<t.phase<<",\"bytes\":"<<t.bytes<<",\"total\":"<<number(t.total)<<",\"speed\":"<<std::fixed<<std::setprecision(2)<<t.speed<<",\"error\":";
  if(!t.errorCode.empty())o<<"{\"code\":"<<quote(t.errorCode)<<",\"message\":"<<quote(t.errorMessage)<<",\"retryable\":"<<json_bool(t.retryable)<<'}';else o<<"null";
 o<<",\"files\":"<<files_json(t.files)<<",\"capabilities\":{\"pause\":"<<json_bool(t.pause)<<",\"resume\":"<<json_bool(t.resume)<<",\"retry\":"<<json_bool(t.retry)<<",\"remove\":"<<json_bool(t.remove)<<",\"selectFiles\":"<<json_bool(t.selectFiles)<<"},\"bt\":{\"dhtNodes\":"<<t.dhtNodes<<",\"peers\":"<<t.peers<<",\"candidates\":"<<t.candidates<<",\"trackerErrors\":"<<t.trackerErrors<<"},\"digest\":"<<(t.digest.empty()?"null":quote(t.digest))<<",\"progressSource\":\"task-service-exact\",\"mode\":\"real\",\"pauseRequested\":"<<json_bool(t.pauseRequested);
 o<<",\"recovery\":{\"state\":"<<quote(t.recovery.state)<<",\"message\":"<<quote(t.recovery.message)<<",\"canResume\":"<<json_bool(t.recovery.canResume)<<",\"canRetry\":"<<json_bool(t.recovery.canRetry)<<"}}";return o.str();
}
int wmain(int argc,wchar_t**argv){
 auto protocol_buf=std::cout.rdbuf();std::ostream protocol(protocol_buf);std::cout.rdbuf(std::cerr.rdbuf());
 try{
  if(argc!=3||std::wstring(argv[1])!=L"--data-dir")throw std::runtime_error("usage: task_service_host --data-dir DIRECTORY");
  core::TaskService service;service.data_dir=fs::absolute(argv[2]).lexically_normal();fs::create_directories(service.data_dir);
  service.instance_lock=std::make_unique<core::TaskLock>(service.data_dir/L"ui.lock");service.load_tasks();
  auto input=GetStdHandle(STD_INPUT_HANDLE);std::string buffer;bool quitting=false;
  while(!quitting){
   service.poll();service.start_next();
   DWORD available=0;
   if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)){
    auto error=GetLastError();if(error==ERROR_BROKEN_PIPE||error==ERROR_INVALID_HANDLE){break;}throw std::runtime_error("host requires piped stdin");
   }
   if(!available){Sleep(20);continue;}
   char bytes[8192];DWORD read=0;if(!ReadFile(input,bytes,std::min<DWORD>(available,sizeof(bytes)),&read,nullptr))break;buffer.append(bytes,read);
   if(buffer.size()>1024*1024)throw std::runtime_error("request too large");
   size_t at;
   while((at=buffer.find('\n'))!=std::string::npos){
    auto line=buffer.substr(0,at);buffer.erase(0,at+1);std::string requestId,op;
    try{
     boost::property_tree::ptree req;std::istringstream in(line);boost::property_tree::read_json(in,req);requestId=req.get<std::string>("requestId","");op=req.get<std::string>("op");std::ostringstream result;
     if(op=="list"){service.sample();service.publish_changes(true);result<<"\"schemaVersion\":3,\"instanceId\":"<<quote(service.instance_id())<<",\"latestSeq\":"<<service.latest_seq()<<",\"tasks\":[";bool first=true;for(auto const&t:service.snapshots()){if(!first)result<<',';first=false;result<<snapshot(t);}result<<']';}
     else if(op=="events"){auto batch=service.events(req.get<uint64_t>("afterSeq",0),req.get<std::string>("instanceId",""));result<<"\"schemaVersion\":3,\"instanceId\":"<<quote(batch.instanceId)<<",\"latestSeq\":"<<batch.latestSeq<<",\"resetRequired\":"<<json_bool(batch.resetRequired)<<",\"events\":[";bool first=true;for(auto const&e:batch.events){if(!first)result<<',';first=false;result<<"{\"seq\":"<<e.seq<<",\"type\":"<<quote(e.type)<<",\"taskId\":"<<e.taskId;if(e.task)result<<",\"task\":"<<snapshot(*e.task);result<<'}';}result<<']';}
     else if(op=="create"){auto url=req.get<std::string>("url");if(url.empty()||url.size()>8192)throw std::runtime_error("invalid URL length");auto ids=service.create(browser_bridge::wide(url),req.get<std::string>("destination", "").empty()?service.data_dir/L"Downloads":fs::u8path(req.get<std::string>("destination")));result<<"\"id\":"<<ids.back();service.start_next();}
     else if(op=="torrent") {
      auto source=fs::u8path(req.get<std::string>("file"));
      if(!fs::is_regular_file(source)||fs::file_size(source)>16*1024*1024)throw std::runtime_error("invalid torrent file (maximum 16 MiB)");
      std::ifstream input(source,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});
      libtorrent::torrent_info info(libtorrent::span<char const>(bytes.data(),bytes.size()),libtorrent::from_span);
      if(!info.info_hashes().has_v1()||info.info_hashes().has_v2())throw std::runtime_error("only BT v1 torrents supported");
      core::validate_bt_files(info.files());
      auto uri=libtorrent::make_magnet_uri(info);int id=0;
      for(auto const&t:service.legacy_tasks())if(t->url==uri){id=t->id;break;}
      if(!id){
       auto destination=req.get<std::string>("destination", "");
       id=service.create(browser_bridge::wide(uri),destination.empty()?service.data_dir/L"Downloads":fs::u8path(destination)).back();
       auto&t=service.find(id);t.status=2;service.persist();
       try {
        auto root=fs::u8path(t.path);fs::create_directories(root);
        std::ostringstream owner;owner<<info.info_hashes().v1<<'\n'<<root.u8string();
        core::save_record(root/L"magnet.owner",owner.str());core::save_record(root/L"metadata.record",bytes);
        t.inventory.event(root/L"magnet.owner",{});t.inventory.event(root/L"metadata.record",{});t.inventory.save();
        t.status=5;t.total=info.total_size();service.persist();service.publish_changes(true);
       }catch(std::exception const&e){t.status=4;t.error=e.what();service.persist();throw;}
      }
      result<<"\"id\":"<<id;
     }
     else if(op=="pause"||op=="resume"||op=="retry"){service.action(req.get<int>("id"),op);result<<"\"accepted\":true";service.start_next();}
     else if(op=="files"){auto&t=service.find(req.get<int>("id"));result<<"\"files\":"<<files_json(service.snapshot(t).files)<<",\"canSelect\":"<<(core::TaskService::can_select(t)?"true":"false");}
     else if(op=="select"){auto&t=service.find(req.get<int>("id"));if(!core::TaskService::can_select(t))throw std::runtime_error("file selection unavailable in current state");std::vector<int>indexes;for(auto const&i:req.get_child("indexes"))indexes.push_back(i.second.get_value<int>());service.apply_selection(t,indexes);result<<"\"accepted\":true";service.start_next();}
     else if(op=="shutdown"){service.begin_shutdown();service.shutdown();result<<"\"accepted\":true";quitting=true;}
     else throw std::runtime_error("unknown operation");
     protocol<<"{\"requestId\":"<<quote(requestId)<<",\"ok\":true,"<<result.str()<<"}"<<std::endl;
    }catch(std::exception const&e){auto code=(op=="pause"||op=="resume"||op=="retry"||op=="select")?"INVALID_STATE":"INVALID_REQUEST";protocol<<"{\"requestId\":"<<quote(requestId)<<",\"ok\":false,\"error\":{\"code\":"<<quote(code)<<",\"message\":"<<quote(e.what())<<",\"retryable\":false}}"<<std::endl;}
    if(quitting)break;
   }
  }
  if(!quitting){service.begin_shutdown();service.shutdown();}
  return 0;
 }catch(std::exception const&e){std::cerr<<"TaskService fatal: "<<e.what()<<std::endl;return 1;}
}
