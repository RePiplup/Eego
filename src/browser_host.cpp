#ifdef HBT_VERSIONED_DESKTOP
#include <windows.h>
#include "task_libraries.hpp"
#endif
// Native messaging transport only. It never downloads data or runs a shell.
#include "browser_bridge.hpp"
#include "browser_identity.hpp"
#include "browser_session.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <sstream>
int wmain(int argc,wchar_t **argv) {
  _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
  bool accepted=false;unsigned task_id=0,status=0;bool query=false;
  std::string library;
  try {
    if(argc<2||std::wstring(argv[1])!=browser_bridge::wide(HBT_EXTENSION_ORIGIN))throw std::runtime_error("unapproved extension origin");
    uint32_t length=0;if(!std::cin.read(reinterpret_cast<char*>(&length),4)||length==0||length>65536)throw std::runtime_error("invalid frame");
    std::string bytes(length,0);if(!std::cin.read(bytes.data(),length))throw std::runtime_error("truncated frame");
    std::istringstream stream(bytes);boost::property_tree::ptree json;boost::property_tree::read_json(stream,json);
    if(json.empty())throw std::runtime_error("invalid message");
#ifdef HBT_VERSIONED_DESKTOP
    auto cls=browser_bridge::window_class(libraries::initial(browser_bridge::home()));
#else
    auto cls=browser_bridge::window_class(browser_bridge::home()/L"userdata");
#endif
    library=browser_bridge::utf8(cls);
    if(json.get_child_optional("task_id")) {
      browser_session::fields(json,{"task_id","library"});
      if(auto token=json.get_optional<std::string>("library");token && *token!=library)
        throw std::runtime_error("task library changed");
      auto value=json.get<std::string>("task_id");
      if(value.empty()||value.size()>9||value.find_first_not_of("0123456789")!=value.npos)throw std::runtime_error("invalid task id");
      task_id=std::stoul(value);query=true;status=unsigned(browser_bridge::task_status(task_id,cls));accepted=true;
    } else if(json.get_child_optional("url")) {
      auto request=browser_session::parse(json);
      std::ostringstream canonical;boost::property_tree::write_json(canonical,json,false);
      task_id=unsigned(browser_bridge::handoff(browser_bridge::wide(request.url),browser_bridge::wide(canonical.str()),cls));accepted=task_id>0;
    } else throw std::runtime_error("unsupported message");
  }catch(std::exception const&){}
  std::string result=accepted?"{\"accepted\":true}":"{\"accepted\":false,\"error\":\"handoff_rejected\"}";
  if(accepted)result="{\"accepted\":true,\"task_id\":"+std::to_string(task_id)+",\"status\":"+std::to_string(status)+",\"library\":\""+library+"\"}";
  uint32_t size=uint32_t(result.size());std::cout.write(reinterpret_cast<char*>(&size),4);std::cout.write(result.data(),result.size());std::cout.flush();return accepted?0:1;
}
