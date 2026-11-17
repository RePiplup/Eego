#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <cstdint>
namespace browser_bridge {
inline std::filesystem::path home() {wchar_t p[32768];if(!GetModuleFileNameW(nullptr,p,32768))throw std::runtime_error("module path unavailable");auto root=std::filesystem::path(p).parent_path();return root.filename()==L"bin"?root.parent_path():root;}
inline std::wstring window_class(std::filesystem::path const&dir) {
  auto path=std::filesystem::absolute(dir).lexically_normal().wstring();uint64_t hash=1469598103934665603ull;
  for(auto c:path){if(c>=L'A'&&c<=L'Z')c+=32;hash^=uint16_t(c);hash*=1099511628211ull;}
  return L"HandmadeBTWindow-"+std::to_wstring(hash);
}
inline std::wstring wide(std::string const&s) {
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(n<=0)throw std::runtime_error("invalid UTF-8");
  std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n);return w;
}
inline std::string utf8(std::wstring const&w) {
  int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);if(n<=0)throw std::runtime_error("invalid Unicode");
  std::string s(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);return s;
}
inline std::wstring normalize(std::wstring value) {
  if(value.rfind(L"handmadebt:",0)==0) {
    auto s=utf8(value.substr(11));std::string decoded;
    auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    for(size_t i=0;i<s.size();++i){if(s[i]=='%'){if(i+2>=s.size()||digit(s[i+1])<0||digit(s[i+2])<0)throw std::runtime_error("invalid protocol escape");decoded+=char(digit(s[i+1])*16+digit(s[i+2]));i+=2;}else decoded+=s[i];}
    value=wide(decoded);
  }
  if(value.empty()||value.size()>8192)throw std::runtime_error("invalid URL length");
  if(value.rfind(L"http://",0)!=0&&value.rfind(L"https://",0)!=0&&value.rfind(L"magnet:?",0)!=0)throw std::runtime_error("unsupported browser URL");
  for(auto c:value)if(c<=32||c==L'|'||c==L'"')throw std::runtime_error("unsafe browser URL");
  return value;
}
inline std::wstring quote(std::wstring const&s) {
  std::wstring out=L"\"";size_t slashes=0;
  for(auto c:s){if(c==L'\\'){++slashes;continue;}out.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;if(c==L'"')out+=L'\\';out+=c;}
  out.append(slashes*2,L'\\');return out+L'"';
}
inline bool send(HWND window,std::wstring const&url) {
  COPYDATASTRUCT data{};data.dwData=0x48425436;data.cbData=DWORD((url.size()+1)*sizeof(wchar_t));data.lpData=const_cast<wchar_t*>(url.c_str());DWORD_PTR accepted=0;
  if(!SendMessageTimeoutW(window,WM_COPYDATA,0,(LPARAM)&data,SMTO_ABORTIFHUNG|SMTO_BLOCK,15000,&accepted))return false;
  return accepted==1;
}
inline DWORD_PTR exchange(HWND target, std::wstring const& value, ULONG_PTR kind) {
  if(!target)return 0;
  COPYDATASTRUCT data{};data.dwData=kind;data.cbData=DWORD((value.size()+1)*sizeof(wchar_t));data.lpData=const_cast<wchar_t*>(value.c_str());DWORD_PTR result=0;
  if(!SendMessageTimeoutW(target,WM_COPYDATA,0,(LPARAM)&data,SMTO_ABORTIFHUNG|SMTO_BLOCK,5000,&result))return 0;
  return result;
}
inline DWORD_PTR task_status(unsigned id, std::wstring const &cls) {
  return exchange(FindWindowW(cls.c_str(),nullptr),std::to_wstring(id),0x48425438);
}
inline DWORD_PTR handoff(std::wstring const&input, std::wstring const&payload, std::wstring const&cls) {
  auto url=normalize(input);auto root=home();
  struct Gate { HANDLE h;bool locked=false;~Gate(){if(locked)ReleaseMutex(h);if(h)CloseHandle(h);} } gate{CreateMutexW(nullptr,FALSE,(L"Local\\"+cls+L"-handoff").c_str())};
  if(!gate.h)return false;auto waited=WaitForSingleObject(gate.h,20000);gate.locked=waited==WAIT_OBJECT_0||waited==WAIT_ABANDONED;
  if(!gate.locked)return false;
  auto target=FindWindowW(cls.c_str(),nullptr);
  if(!target) {
    #ifdef HBT_QT_DESKTOP
    auto exe=root/L"HandmadeBTQuick.exe";
#else
    auto exe=root/L"bin"/L"handmadebt.exe";if(!std::filesystem::exists(exe))exe=root/L"handmadebt.exe";
#endif
    std::wstring cmd=quote(exe.wstring());STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&si,&pi))return false;
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    for(int n=0;n<200&&!target;++n){Sleep(50);target=FindWindowW(cls.c_str(),nullptr);}
  }
#ifdef HBT_VERSIONED_DESKTOP
  if(target) {
    DWORD pid=0;GetWindowThreadProcessId(target,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    wchar_t path[32768];DWORD size=32768;
    bool matched=process && QueryFullProcessImageNameW(process,0,path,&size) &&
        CompareStringOrdinal(path,-1,(root/L"HandmadeBT.exe").c_str(),-1,TRUE)==CSTR_EQUAL;
    if(process)CloseHandle(process);
    if(!matched)return 0;
  }
#endif
  return exchange(target,payload.empty()?url:payload,payload.empty()?0x48425437:0x48425439);
}
inline DWORD_PTR task_status(unsigned id){return task_status(id,window_class(home()/L"userdata"));}
inline DWORD_PTR handoff(std::wstring const &url,std::wstring const &payload=L""){
  return handoff(url,payload,window_class(home()/L"userdata"));
}

}
