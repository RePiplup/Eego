#pragma once
#include "http.hpp"
#include "http_redirect.hpp"
#include "browser_bridge.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <set>
#include <sstream>
namespace browser_session {
using boost::property_tree::ptree;
inline void fields(ptree const& t,std::set<std::string> allowed) {
  for(auto const& v:t)if(!allowed.erase(v.first))throw std::runtime_error("invalid browser request fields");
}
inline std::string text(ptree const&t,char const*key,size_t max) {
  auto const&v=t.get_child(key);auto s=v.get_value<std::string>();
  if(!v.empty()||s.size()>max||s.find_first_of("\r\n")!=s.npos||s.find('\0')!=s.npos)throw std::runtime_error("invalid browser request value");
  return s;
}
inline bool same_origin(std::wstring const&a,std::wstring const&b) {
  auto x=core::checked_http_url(a,false),y=core::checked_http_url(b,false);
  return x.nScheme==y.nScheme&&x.nPort==y.nPort&&CompareStringOrdinal(x.lpszHostName,x.dwHostNameLength,y.lpszHostName,y.dwHostNameLength,TRUE)==CSTR_EQUAL;
}
struct Request {std::string url,filename;core::BrowserSession session;};
inline Request parse(ptree const&t) {
  fields(t,{"url","session","filename"});Request r;
  r.url=text(t,"url",8192);browser_bridge::normalize(browser_bridge::wide(r.url));
  if(t.get_child_optional("filename"))r.filename=text(t,"filename",200);
  if(auto session=t.get_child_optional("session")) {
    fields(*session,{"cookies","referer","userAgent"});
    r.session.url=r.url;auto url=browser_bridge::wide(r.url);core::checked_http_url(url,false);
    r.session.user_agent=text(*session,"userAgent",1024);r.session.referer=text(*session,"referer",8192);
    if(!r.session.referer.empty()&&!same_origin(url,browser_bridge::wide(r.session.referer)))throw std::runtime_error("cross-origin browser referer refused");
    auto const&cookies=session->get_child("cookies");if(!cookies.data().empty()||cookies.size()>200)throw std::runtime_error("too many cookies");
    size_t total=0;
    for(auto const&entry:cookies) {
      if(!entry.first.empty())throw std::runtime_error("invalid cookie array");
      fields(entry.second,{"name","value","path","secure"});core::BrowserCookie c;
      c.name=text(entry.second,"name",256);c.value=text(entry.second,"value",4096);c.path=text(entry.second,"path",2048);
      auto secure=text(entry.second,"secure",5);if(secure!="true"&&secure!="false")throw std::runtime_error("invalid cookie flag");c.secure=secure=="true";
      if(c.name.empty()||c.name.find_first_of("()<>@,;:\\\"/[]?={} \t")!=c.name.npos||c.value.find(';')!=c.value.npos||c.path.empty()||c.path[0]!='/')throw std::runtime_error("invalid cookie");
      for(unsigned char v:c.name+c.value)if(v<32||v>126)throw std::runtime_error("invalid cookie characters");
      total+=c.name.size()+c.value.size()+c.path.size();if(total>24000)throw std::runtime_error("browser session too large");r.session.cookies.push_back(std::move(c));
    }
  }
  return r;
}
}
