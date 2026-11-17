// Explicit per-task creation inventory. Never discover ownership from filenames.
#pragma once
#include "checkpoint.hpp"
#include "file_identity.hpp"
#include <shellapi.h>
#include <shobjidl.h>
#include <map>
#include <mutex>
#include <iomanip>
#include <sstream>
namespace managed {
namespace fs=std::filesystem;
struct Inventory {
  fs::path record,target,output;std::string binding;bool magnet=false,single_output=false;
  std::map<fs::path,std::string> entries;std::mutex mutex;
  bool accepts(fs::path const&p)const {
    if(magnet && !output.empty()){
      auto relative=p.lexically_relative(output);
      if(!relative.empty() && *relative.begin()!=L"..")return true;
    }
    auto a=p.wstring(),b=target.wstring();
    if(magnet)return a.size()>b.size()&&a.substr(0,b.size())==b&&(a[b.size()]==L'\\'||a[b.size()]==L'/');
    return p==target||a==b+L".part"||a==b+L".resume"||a.rfind(b+L".resume.tmp-",0)==0;
  }
  void load() {
    if(!fs::exists(record))return;
    std::istringstream in(core::load_record(record));std::string bind;size_t n;
    if(!(in>>std::quoted(bind)>>n)||bind!=binding||n>100000)throw std::runtime_error("文件归属清单无效，原文件保留");
    for(size_t k=0;k<n;++k){std::string p,id;if(!(in>>std::quoted(p)>>std::quoted(id))||!accepts(fs::u8path(p)))throw std::runtime_error("文件归属清单越界");entries[fs::u8path(p)]=id;}
    in>>std::ws;if(!in.eof())throw std::runtime_error("文件归属清单格式错误");
  }
  void begin() {
    // A retry must not bless user edits made while the task was stopped.
    // Forget altered identities before the core runs; later cleanup fails closed.
    std::lock_guard<std::mutex> guard(mutex);
    for(auto it=entries.begin();it!=entries.end();) {
      bool trusted=false;try{trusted=identity(it->first)==it->second;}catch(...){}
      if(!trusted)it=entries.erase(it);else ++it;
    }
  }
  void event(fs::path const&p,fs::path const&from) {
    if(!accepts(p))return;
    std::lock_guard<std::mutex> guard(mutex);
    auto id=identity(p,false);
    if(!from.empty()){auto it=entries.find(from);if(it==entries.end())return;if(it->second!=id&&it->second.rfind(id+':',0)!=0)throw std::runtime_error("renamed file identity changed");entries.erase(it);}
    entries[p]=id;
  }
  void save() {
    std::lock_guard<std::mutex> guard(mutex);
    for(auto it=entries.begin();it!=entries.end();) {
      if(!fs::exists(it->first)){it=entries.erase(it);continue;}
      auto id=identity(it->first,false);
      if(it->second!=id&&it->second.rfind(id+':',0)!=0)throw std::runtime_error("文件已被替换，停止更新归属清单");
      it->second=identity(it->first);++it;
    }
    std::ostringstream out;out<<std::quoted(binding)<<' '<<entries.size()<<'\n';
    for(auto const&[p,id]:entries)out<<std::quoted(p.u8string())<<' '<<std::quoted(id)<<'\n';
    core::save_record(record,out.str());
  }
  std::vector<fs::path> plan() {
    std::vector<fs::path> out;
    for(auto const&[p,id]:entries)if(fs::exists(p)) {
      if(identity(p)!=id)throw std::runtime_error("文件被修改、替换或清单未完整保存，停止删除："+p.u8string());out.push_back(p);
    }
    // Legacy output/partial files must never be adopted by name.
    if(!magnet)for(auto p:{target,fs::path(target.wstring()+L".part"),fs::path(target.wstring()+L".resume")})
      if(fs::exists(p)&&entries.find(p)==entries.end())throw std::runtime_error("存在没有创建归属记录的旧文件；仅可移除任务，文件保留："+p.u8string());
    auto directory=output.empty()?target:output;
    if(magnet&&!output.empty()&&fs::is_regular_file(output)&&!entries.count(output))throw std::runtime_error("未登记的文件，停止删除");
    if(magnet&&fs::is_directory(directory))for(auto const&e:fs::recursive_directory_iterator(directory)) {
      core::reject_bt_reparse(e.path());
      if(e.is_regular_file()&&e.path().filename()!=L"magnet.lock"&&entries.find(e.path())==entries.end())throw std::runtime_error("任务目录含未登记文件；停止删除并保留全部剩余文件："+e.path().u8string());
    }
    return out;
  }
};
// Veto Shell's permanent-delete fallback. No DeleteFile/SHFileOperation fallback.
struct Sink final:IFileOperationProgressSink {
  struct WindowsPathLess {bool operator()(std::wstring const&a,std::wstring const&b)const{auto x=a,y=b;for(auto &c:x)if(c==L'/')c=wchar_t(92);for(auto &c:y)if(c==L'/')c=wchar_t(92);return CompareStringOrdinal(x.c_str(),-1,y.c_str(),-1,TRUE)==CSTR_LESS_THAN;}};
  ULONG refs=1;bool failed=false;std::map<std::wstring,std::string,WindowsPathLess> expected;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void**p)override{if(riid==IID_IUnknown||riid==IID_IFileOperationProgressSink){*p=this;AddRef();return S_OK;}*p=nullptr;return E_NOINTERFACE;}
  ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;} ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE StartOperations()override{return S_OK;}
  HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT hr)override{failed|=FAILED(hr);return S_OK;}
  HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD,IShellItem*,LPCWSTR)override{return E_ABORT;}
  HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD,IShellItem*,LPCWSTR,HRESULT,IShellItem*)override{return S_OK;}
  HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR)override{return E_ABORT;}
  HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*)override{return S_OK;}
  HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR)override{return E_ABORT;}
  HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*)override{return S_OK;}
  HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags,IShellItem*item)override {
    if(!(flags&TSF_DELETE_RECYCLE_IF_POSSIBLE)){failed=true;return E_ABORT;}
    PWSTR path=nullptr;auto hr=item->GetDisplayName(SIGDN_FILESYSPATH,&path);
    if(FAILED(hr)){failed=true;return E_ABORT;}
    try{auto it=expected.find(path);bool ok=it!=expected.end()&&identity(path)==it->second;CoTaskMemFree(path);if(ok)return S_OK;}
    catch(...){CoTaskMemFree(path);}failed=true;return E_ABORT;
  }
  HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD,IShellItem*,HRESULT hr,IShellItem*newItem)override{failed|=FAILED(hr)||!newItem;return failed?E_ABORT:S_OK;}
  HRESULT STDMETHODCALLTYPE PreNewItem(DWORD,IShellItem*,LPCWSTR)override{return E_ABORT;}
  HRESULT STDMETHODCALLTYPE PostNewItem(DWORD,IShellItem*,LPCWSTR,LPCWSTR,DWORD,HRESULT,IShellItem*)override{return S_OK;}
  HRESULT STDMETHODCALLTYPE UpdateProgress(UINT,UINT)override{return S_OK;}
  HRESULT STDMETHODCALLTYPE ResetTimer()override{return S_OK;} HRESULT STDMETHODCALLTYPE PauseTimer()override{return S_OK;} HRESULT STDMETHODCALLTYPE ResumeTimer()override{return S_OK;}
};
inline void recycle(std::vector<fs::path> paths,HWND owner) {
  for(auto &p:paths)p=fs::absolute(p).lexically_normal().make_preferred();
  if(paths.empty())return;
  auto sink=new Sink;IFileOperation*op=nullptr;
  auto check=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("回收站操作失败（错误码 "+std::to_string(static_cast<unsigned long>(hr))+"），已停止；没有永久删除。部分文件可能已回收，请检查回收站。");};
  try {
    for(auto const&p:paths){wchar_t volume[MAX_PATH];if(!GetVolumePathNameW(p.c_str(),volume,MAX_PATH)||GetDriveTypeW(volume)!=DRIVE_FIXED)throw std::runtime_error("仅支持本地固定磁盘回收；此位置无法可靠回收，停止删除");sink->expected[p.wstring()]=identity(p);}
    check(CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&op)));
    check(op->SetOwnerWindow(owner));check(op->SetOperationFlags(FOFX_RECYCLEONDELETE|FOFX_EARLYFAILURE|FOF_NOERRORUI|FOF_SILENT|FOF_NOCONFIRMATION|FOFX_ADDUNDORECORD));
    for(auto const&p:paths){IShellItem*item=nullptr;check(SHCreateItemFromParsingName(p.c_str(),nullptr,IID_PPV_ARGS(&item)));auto hr=op->DeleteItem(item,sink);item->Release();check(hr);}
    check(op->PerformOperations());BOOL aborted=FALSE;check(op->GetAnyOperationsAborted(&aborted));if(aborted||sink->failed)check(E_ABORT);
    op->Release();sink->Release();
  }catch(...){if(op)op->Release();sink->Release();throw;}
}
}

