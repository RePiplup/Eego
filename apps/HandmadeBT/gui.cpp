// HandmadeBT minimal native Windows frontend. GPL-2.0-or-later; see COPYING.
// Downloads execute in-process through download_core, never through core_cli.
#include "task_service.hpp"
#include "http.hpp"
#include "magnet.hpp"
#include "browser_bridge.hpp"
#include "browser_session.hpp"
#include "checkpoint.hpp"
#include "file_store.hpp"
#include "managed_files.hpp"
#include <set>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include "task_libraries.hpp"
#include <shlobj.h>
#include <libtorrent/hasher.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr wchar_t version[] = L"HandmadeBT";
enum { Links=101, Destination=102, Add=103, Pause=104, Resume=105,
       Tasks=106, Details=107, Browse=108, Open=109, Remove=110, Files=111, Hint=112, Filter=113, SelectAll=114, Retry=115, OpenFile=116, Title, Overview, LinkLabel, FolderLabel, QueueLabel, Empty };
using Task=core::ServiceTask;
core::TaskService task_service;
auto const &tasks=task_service.legacy_tasks();
std::map<int,std::vector<std::wstring>> painted;
auto &instance_lock=task_service.instance_lock;
auto &data_dir=task_service.data_dir;
HWND window,links,destination,list,details,add_button,pause_button,resume_button,remove_button,files_button;
HFONT font, title_font, heading_font;
int dpi=96;
int px(int dip){return MulDiv(dip,dpi,96);}

auto &next_id=task_service.next_id;
auto &closing=task_service.closing;auto &storage_failed=task_service.storage_failed;
bool refreshing=false, removal_busy=false;
int category=0;
std::vector<Task*> visible;
std::wstring startup_url;

std::wstring wide(std::string const &s) {
  if(s.empty()) return {};
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
  if(!n) throw std::runtime_error("invalid UTF-8 text");
  std::wstring w(n,0);
  MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n);
  return w;
}
std::string utf8(std::wstring const &w) {
  if(w.empty()) return {};
  int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);
  if(!n) throw std::runtime_error("invalid Unicode text");
  std::string s(n,0);
  WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);
  return s;
}
std::wstring text(HWND h) {
  int n=GetWindowTextLengthW(h); std::wstring s(n+1,0);
  GetWindowTextW(h,s.data(),n+1); s.resize(n); return s;
}
void caption(HWND h,std::wstring const &s){if(text(h)!=s)SetWindowTextW(h,s.c_str());}
std::wstring trim(std::wstring s);
void message(std::string const &s) { MessageBoxW(window,wide(s).c_str(),version,MB_OK|MB_ICONERROR); }
std::string file_hash(Task const&t){return core::TaskService::file_hash(t);}
void init_inventory(Task&t){task_service.init_inventory(t);}
void save_tasks(){task_service.save_tasks();}
void persist(){try{task_service.persist();}catch(std::exception const&e){message(std::string("任务列表保存失败，已停止新任务：")+e.what());}}
void load_tasks(){task_service.load_tasks();}
std::vector<Task*> selection() {
  std::vector<Task*> result;if(!list)return result;
  for(int i=-1;(i=ListView_GetNextItem(list,i,LVNI_SELECTED))>=0;)if(size_t(i)<visible.size())result.push_back(visible[i]);
  return result;
}
Task *selected() {auto v=selection();return v.size()==1?v.front():nullptr;}
std::wstring amount(double n) {
  wchar_t const*units[]={L"B",L"KiB",L"MiB",L"GiB",L"TiB"};int i=0;while(n>=1024&&i<4){n/=1024;++i;}
  std::wostringstream out;out<<std::fixed<<std::setprecision(i?1:0)<<n<<L" "<<units[i];return out.str();
}
std::wstring state(Task const &t) {
  if(t.removing)return L"正在停止并移除…";
  if(t.status==0)return L"等待下载";
  if(t.status==2)return L"已暂停";
  if(t.status==3)return t.expected.empty()?L"已完成 · 写入校验通过":L"已完成 · SHA-256 通过";
  if(t.status==4)return L"失败 · 可重试";
  if(t.status==5)return L"请选择下载文件";
  if(t.status==6)return L"暂未找到元数据节点";
  if(t.control.pause)return L"正在暂停…";
  if(t.control.phase==2)return L"正在校验文件…";
  if(t.control.phase==5)return L"获取磁力元数据…";
  if(t.control.phase==6)return L"等待做种节点…";
  if(t.control.phase==0)return L"连接 / 重验断点…";
  return !t.control.ranged && t.url.rfind("magnet:?",0)!=0 ? L"下载中 · 恢复需重下" : L"下载中";
}
void refresh() {
  if(!list||!details||refreshing)return; // EDIT initialization can send WM_COMMAND before the list exists.
  refreshing=true;
  task_service.sample();
  auto picked=selection();std::vector<Task*> next;
  for(auto &p:tasks)if(category==0||(category==1&&p->status!=3)||(category==2&&p->status==3))next.push_back(p.get());
  if(next!=visible){SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);visible=next;for(auto*t:visible)painted[t->id].clear();}
  for(size_t i=0;i<visible.size();++i) {
    auto &t=*visible[i];
    if(int(i)>=ListView_GetItemCount(list)) {
      std::wstring id=std::to_wstring(t.id);LVITEMW item{};
      item.mask=LVIF_TEXT;item.iItem=int(i);item.pszText=id.data();ListView_InsertItem(list,&item);
    }
    std::wostringstream progress;
    progress<<amount(t.bytes)<<L" / "<<(t.total?amount(t.total):L"未知大小");
    if(t.total)progress<<L"  ("<<std::fixed<<std::setprecision(1)<<100.0*t.bytes/t.total<<L"%)";
    std::wstring cells[]={fs::u8path(t.path).filename().wstring(),state(t),progress.str(),amount(t.speed)+L"/s"};
    auto &cache=painted[t.id];cache.resize(4);
    for(int c=0;c<4;++c)if(cache[c]!=cells[c]){ListView_SetItemText(list,int(i),c,cells[c].data());cache[c]=cells[c];}
  }
  for(size_t i=0;i<visible.size();++i)if(std::find(picked.begin(),picked.end(),visible[i])!=picked.end())ListView_SetItemState(list,int(i),LVIS_SELECTED,LVIS_SELECTED);
  SendMessageW(list,WM_SETREDRAW,TRUE,0);
  auto chosen=selection();auto t=selected();
  auto any=[&](auto pred){return !closing&&!storage_failed&&std::any_of(chosen.begin(),chosen.end(),[&](Task*p){return !p->removing&&pred(*p);});};
  EnableWindow(pause_button,any([](Task&t){return t.status==0||(t.status==1&&!t.control.pause);}));
  EnableWindow(resume_button,any([](Task&t){return t.status==2;}));
  EnableWindow(GetDlgItem(window,Retry),any([](Task&t){return t.status==4||t.status==6;}));
  EnableWindow(remove_button,any([](Task&){return true;}));
  EnableWindow(files_button,t&&t->url.rfind("magnet:?",0)==0&&t->status!=1&&!t->removing&&fs::exists(fs::u8path(t->path)/L"metadata.record")&&!closing);
  EnableWindow(GetDlgItem(window,OpenFile),t&&t->status==3&&!closing);
  EnableWindow(GetDlgItem(window,Open),t&&!closing);
  EnableWindow(add_button,!closing&&!storage_failed&&!trim(text(links)).empty()&&!trim(text(destination)).empty());
  EnableWindow(GetDlgItem(window,SelectAll),!visible.empty());
  int active=0,paused=0,done=0,failed=0;
  for(auto const &entry:tasks){
    switch(entry->status){case 1:++active;break;case 2:++paused;break;case 3:++done;break;case 4:case 6:++failed;break;}
  }
  caption(GetDlgItem(window,Overview),L"共 "+std::to_wstring(tasks.size())+L" 项    运行 "+std::to_wstring(active)+L"    暂停 "+std::to_wstring(paused)+L"    完成 "+std::to_wstring(done)+L"    异常 "+std::to_wstring(failed));
  caption(GetDlgItem(window,Hint),chosen.empty()?L"任务详情":L"任务详情  ·  已选 "+std::to_wstring(chosen.size())+L" 项");
  caption(GetDlgItem(window,Empty),tasks.empty()?L"还没有下载任务。粘贴链接，点击“添加下载”即可开始。":L"当前分类没有任务。");
  ShowWindow(list,visible.empty()?SW_HIDE:SW_SHOWNA);
  ShowWindow(GetDlgItem(window,Empty),visible.empty()?SW_SHOWNA:SW_HIDE);
  if(t) {
    std::wstring s=state(*t)+L"  ·  "+fs::u8path(t->path).filename().wstring()+L"\r\n";
    if(!t->error.empty())s+=L"错误："+wide(t->error)+L"\r\n";
    s+=L"已接收："+std::to_wstring(t->bytes)+L" 字节 / "+(t->total?std::to_wstring(t->total)+L" 字节":L"未知大小")+L"\r\n";
    s+=L"保存到："+wide(t->path)+L"\r\n链接："+wide(t->url)+L"\r\n";
    if(t->status==1&&t->speed>0&&t->total>t->bytes)s+=L"预计剩余："+std::to_wstring(uint64_t((t->total-t->bytes)/t->speed))+L" 秒\r\n";
    if(!t->digest.empty())s+=(t->url.rfind("magnet:?",0)==0?L"逐文件已校验；清单 SHA-256：":L"实际 SHA-256：")+wide(t->digest)+L"\r\n";
    if(!t->expected.empty())s+=L"期望 SHA-256："+wide(t->expected)+L"\r\n";
    auto snapshot=task_service.snapshot(*t);
    if(!snapshot.recovery.message.empty())s+=wide(snapshot.recovery.message)+L"\r\n";
    if(t->url.rfind("magnet:?",0)==0&&t->status==1)s+=L"已连接下载节点："+std::to_wstring(t->control.bt_peers)+L"\r\n";
    if(t->status==1&&t->control.phase==5){
      s+=L"正在查找文件清单，取得后将打开文件选择。\r\n";
      s+=L"发现网络节点："+std::to_wstring(snapshot.dhtNodes)+L"　已知下载节点："+std::to_wstring(snapshot.candidates)+L"\r\n";
      if(!snapshot.dhtNodes&&!snapshot.candidates)s+=L"正在通过 DHT 寻找可提供文件清单的节点。\r\n";
    }

    if(t->status==1&&!t->control.ranged&&t->url.rfind("magnet:?",0)!=0)s+=L"\r\n连接确认前或服务器不支持分段时，恢复可能从零重下。";
    if(text(details)!=s)SetWindowTextW(details,s.c_str());
  }
  else caption(details,chosen.empty()?L"选择任务查看路径、错误和 SHA-256。":L"已选择多个任务；操作仅作用于当前分类内选中的适用任务。\r\n下载中分类包含等待、暂停、失败及待选择文件的未完成任务。");
  refreshing=false;
}
void remove_stopped() {
  if(removal_busy)return;
  struct Guard{Guard(){removal_busy=true;}~Guard(){removal_busy=false;}} guard;
  try {
    if(task_service.remove_stopped([](auto const&plan){managed::recycle(plan,window);})) {
      refreshing=true;visible.clear();ListView_DeleteAllItems(list);refreshing=false;painted.clear();refresh();InvalidateRect(list,nullptr,FALSE);
    }
  }catch(std::exception const&e){refreshing=true;visible.clear();ListView_DeleteAllItems(list);refreshing=false;painted.clear();refresh();message(e.what());}

}
void start_next(){if(!removal_busy)task_service.start_next();}
void choose_files(Task &t);
std::set<int> prompted_tasks;
bool choosing_files=false;
void tick(){
  if(removal_busy)return;
  bool active=task_service.poll();
  if(!storage_failed)remove_stopped();
  refresh();
  if(closing&&!active){DestroyWindow(window);return;}
  start_next();
  if(closing||storage_failed||choosing_files||!IsWindowEnabled(window))return;
  for(auto const &task:tasks){
    if(task->status==5&&!task->removing&&prompted_tasks.insert(task->id).second){
      choose_files(*task);
      return;
    }
  }
}
std::wstring trim(std::wstring s) {
  auto a=s.find_first_not_of(L" \t\r\n");if(a==s.npos)return {};
  return s.substr(a,s.find_last_not_of(L" \t\r\n")-a+1);
}
void add_tasks(std::wstring const&external=L"",std::wstring const&suggested=L""){
  if(closing||storage_failed)return;
  task_service.create(external.empty()?text(links):external,fs::path(trim(text(destination))),suggested);
  if(external.empty()){SetWindowTextW(links,L"");refresh();start_next();}
  else PostMessageW(window,WM_APP+2,0,0);
}
HWND control(wchar_t const *cls,wchar_t const *value,DWORD style,int x,int y,int w,int h,int id) {
  HWND c=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,value,WS_CHILD|WS_VISIBLE|style,
      x,y,w,h,window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
  SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
}
void apply_selection(Task&t,std::vector<int>const&indexes){task_service.apply_selection(t,indexes);}
struct FileChoice {Task *task; HWND list=nullptr; HWND summary=nullptr;std::vector<core::MagnetFile> files;bool editable=false;bool bulk_update=false;};
void update_choice(FileChoice &choice){
  int count=0;
  uint64_t bytes=0;
  for(size_t i=0;i<choice.files.size();++i){
    if(ListView_GetCheckState(choice.list,int(i))){++count;bytes+=choice.files[i].size;}
  }
  auto caption=L"已选 "+std::to_wstring(count)+L" / "+std::to_wstring(choice.files.size())+L" 个文件，共 "+amount(double(bytes));
  SetWindowTextW(choice.summary,caption.c_str());
  EnableWindow(GetDlgItem(GetParent(choice.list),5),count>0);
}
LRESULT CALLBACK choice_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
  auto c=reinterpret_cast<FileChoice*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(msg==WM_CREATE) {
    c=static_cast<FileChoice*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)c);
    auto make=[&](wchar_t const*cls,wchar_t const*title,DWORD style,int x,int y,int w,int height,int id){auto v=CreateWindowW(cls,title,WS_VISIBLE|WS_CHILD|style,x,y,w,height,h,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(v,WM_SETFONT,(WPARAM)font,TRUE);return v;};
    make(L"STATIC",c->editable?L"勾选要保存的文件，然后点击“下载选中文件”。":L"文件清单（只读）。已开始的任务若需不同选择，请新建任务；原文件保留。",0,12,12,750,48,0);
    c->list=make(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SHOWSELALWAYS|WS_TABSTOP,12,62,750,350,1);
    ListView_SetExtendedListViewStyle(c->list,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    wchar_t const* names[]={L"文件",L"大小"};int widths[]={570,155};
    for(int i=0;i<2;++i){LVCOLUMNW col{};col.mask=LVCF_WIDTH|LVCF_TEXT;col.cx=widths[i];col.pszText=const_cast<wchar_t*>(names[i]);ListView_InsertColumn(c->list,i,&col);}
    for(size_t i=0;i<c->files.size();++i){auto name=wide(c->files[i].path);if(!c->editable&&!c->files[i].selected)name=L"[未下载] "+name;LVITEMW row{};row.mask=LVIF_TEXT;row.iItem=int(i);row.pszText=name.data();ListView_InsertItem(c->list,&row);auto size=amount(double(c->files[i].size));ListView_SetItemText(c->list,int(i),1,size.data());ListView_SetCheckState(c->list,int(i),(!c->editable||c->task->status!=5)&&c->files[i].selected);}
    if(!c->editable){ListView_SetExtendedListViewStyle(c->list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);if(c->task->status==3){make(L"BUTTON",L"打开选中文件",WS_TABSTOP,12,428,150,32,6);make(L"BUTTON",L"所在文件夹",WS_TABSTOP,174,428,150,32,7);}}
    if(c->editable){make(L"BUTTON",L"全选",WS_TABSTOP,12,428,90,32,3);make(L"BUTTON",L"全不选",WS_TABSTOP,112,428,90,32,4);make(L"BUTTON",L"下载选中文件",WS_TABSTOP,470,428,160,32,5);}
    if(c->editable){c->summary=make(L"STATIC",L"",0,12,37,750,24,8);update_choice(*c);}
    make(L"BUTTON",c->editable?L"稍后选择":L"关闭",WS_TABSTOP,642,428,120,32,IDCANCEL);return 0;
  }
  if(msg==WM_NOTIFY&&c&&c->summary&&!c->bulk_update){
    auto header=reinterpret_cast<NMHDR*>(lp);
    if(header->hwndFrom==c->list&&header->code==LVN_ITEMCHANGED)update_choice(*c);
  }
  if(msg==WM_COMMAND&&c) {
    int id=LOWORD(wp);
    if((id==3||id==4)&&c->editable){c->bulk_update=true;for(int i=0;i<int(c->files.size());++i)ListView_SetCheckState(c->list,i,id==3);c->bulk_update=false;update_choice(*c);}
    if(id==5&&c->editable)try {
      std::vector<int> indexes;for(int i=0;i<int(c->files.size());++i)if(ListView_GetCheckState(c->list,i))indexes.push_back(c->files[i].index);
      apply_selection(*c->task,indexes);DestroyWindow(h);return 0;
    }catch(std::exception const&e){MessageBoxW(h,wide(e.what()).c_str(),L"文件选择",MB_OK|MB_ICONERROR);}
    if((id==6||id==7)&&c->task->status==3)try {
      int row=ListView_GetNextItem(c->list,-1,LVNI_SELECTED);if(row<0)throw std::runtime_error("请先选择一个已下载文件");
      if(!c->files.at(row).selected)throw std::runtime_error("此文件未选择下载");
      auto path=fs::u8path(c->task->path)/L"files"/fs::u8path(c->files.at(row).path);
      if(id==7)path=path.parent_path();
      if(!fs::exists(path)||(INT_PTR)ShellExecuteW(h,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error("文件不存在或 Windows 无法打开");
    }catch(std::exception const&e){MessageBoxW(h,wide(e.what()).c_str(),L"打开文件",MB_OK|MB_ICONERROR);}
    if(id==IDCANCEL)DestroyWindow(h);return 0;
  }
  if(msg==WM_CLOSE){DestroyWindow(h);return 0;}
  return DefWindowProcW(h,msg,wp,lp);
}
void choose_files(Task &t) {
  if(choosing_files)return;
  struct Guard { Guard(){choosing_files=true;} ~Guard(){choosing_files=false;} } guard;
  prompted_tasks.insert(t.id);
  FileChoice c{&t};c.files=core::TaskService::files(t);c.editable=core::TaskService::can_select(t);
  WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=choice_proc;cls.lpszClassName=L"HandmadeBTFileChoice";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&cls);
  auto dialog=CreateWindowW(cls.lpszClassName,L"选择下载文件",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,800,515,window,nullptr,cls.hInstance,&c);
  if(!dialog)throw std::runtime_error("cannot create file choice window");
  EnableWindow(window,FALSE);MSG msg;while(IsWindow(dialog)&&GetMessageW(&msg,nullptr,0,0)>0)if(!IsDialogMessageW(dialog,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
  EnableWindow(window,TRUE);SetForegroundWindow(window);refresh();start_next();
}
void layout(int width,int height) {
  if(!list)return;
  int w=MulDiv(width,96,dpi), h=MulDiv(height,96,dpi);
  auto place=[&](int id,int x,int y,int cw,int ch){
    MoveWindow(GetDlgItem(window,id),px(x),px(y),px(cw),px(ch),TRUE);
  };
  const int margin=18, inner=w-2*margin;
  place(Title,margin,10,220,36);place(Overview,230,17,w-248,26);
  place(LinkLabel,margin,56,88,44);place(Links,108,53,w-252,46);
  place(Add,w-132,53,114,46);
  place(FolderLabel,margin,112,88,27);place(Destination,108,107,w-252,30);
  place(Browse,w-132,107,114,30);
  place(QueueLabel,margin,158,120,30);place(Filter,w-268,153,142,160);place(SelectAll,w-118,153,100,30);
  const int tableTop=192, tableHeight=std::max(80,(h-tableTop-100)*57/100);
  place(Tasks,margin,tableTop,inner,tableHeight);
  place(Empty,margin+4,tableTop+(tableHeight+20)/2-14,inner-8,30);
  const int cols=px(inner)-GetSystemMetrics(SM_CXVSCROLL)-6;
  int widths[]={cols*31/100,cols*23/100,cols*32/100,0};
  widths[3]=cols-widths[0]-widths[1]-widths[2];
  for(int i=0;i<4;++i)ListView_SetColumnWidth(list,i,widths[i]);
  int y=tableTop+tableHeight+9;
  int ids[]={Pause,Resume,Retry,Files,OpenFile,Open,Remove};
  int x=margin;
  for(int id:ids){
    int bw=(id==Files||id==Open)?100:70;
    if(id==Remove)x=w-margin-bw;
    place(id,x,y,bw,32);x+=bw+6;
  }
  place(Hint,margin,y+43,inner,26);place(Details,margin,y+71,inner,h-y-83);

}
constexpr int browse_library=31000;
constexpr int show_library=31001;
constexpr int first_library=31100;
std::vector<libraries::Library> known_libraries;
void open_library(fs::path const &folder){
  if(libraries::same(folder,data_dir)){
    libraries::remember(browser_bridge::home(),folder);
    return;
  }
  wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,32768);
  auto args=L"--use-library "+browser_bridge::quote(folder.wstring());
  if((INT_PTR)ShellExecuteW(window,L"open",executable,args.c_str(),nullptr,SW_SHOWNORMAL)<=32)
    throw std::runtime_error("打开任务库失败");
}
void library_menu(HWND owner){
  auto menu=CreateMenu();auto popup=CreatePopupMenu();
  AppendMenuW(popup,MF_STRING,show_library,L"查看当前任务库位置");
  AppendMenuW(popup,MF_STRING,browse_library,L"打开其他任务库…");
  AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
  known_libraries=libraries::discover(browser_bridge::home());
  for(size_t i=0;i<known_libraries.size()&&i<500;++i){
    auto const &lib=known_libraries[i];
    auto title=lib.path.parent_path().filename().wstring()+L"（"+std::to_wstring(lib.count)+L" 项）";
    AppendMenuW(popup,MF_STRING|(libraries::same(lib.path,data_dir)?MF_CHECKED:0),first_library+i,title.c_str());
  }
  AppendMenuW(menu,MF_POPUP,(UINT_PTR)popup,L"任务库");SetMenu(owner,menu);
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
  try {
    if(msg==WM_CREATE) {
      window=h;library_menu(h);auto make_font=[](int size,int weight){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");};
      font=make_font(15,FW_NORMAL);title_font=make_font(24,FW_BOLD);heading_font=make_font(17,FW_SEMIBOLD);
      auto title=control(L"STATIC",L"下载中心",SS_CENTERIMAGE,0,0,0,0,Title);
      SendMessageW(title,WM_SETFONT,(WPARAM)title_font,TRUE);
      control(L"STATIC",L"",SS_RIGHT|SS_CENTERIMAGE,0,0,0,0,Overview);
      control(L"STATIC",L"下载链接\r\n每行一条",0,0,0,0,0,LinkLabel);
      links=control(L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN,16,40,910,88,Links);
      control(L"STATIC",L"保存目录",SS_CENTERIMAGE,0,0,0,0,FolderLabel);
      destination=control(L"EDIT",(data_dir/L"Downloads").c_str(),WS_TABSTOP|ES_AUTOHSCROLL,106,136,640,29,Destination);
      control(L"BUTTON",L"浏览…",WS_TABSTOP,756,136,80,30,Browse);
      add_button=control(L"BUTTON",L"添加下载",WS_TABSTOP|BS_DEFPUSHBUTTON,842,136,84,30,Add);
      auto queue=control(L"STATIC",L"任务列表",SS_CENTERIMAGE,0,0,0,0,QueueLabel);
      SendMessageW(queue,WM_SETFONT,(WPARAM)heading_font,TRUE);
      auto filter=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,16,181,290,180,Filter);
      for(auto title:{L"全部",L"未完成",L"已完成"})SendMessageW(filter,CB_ADDSTRING,0,(LPARAM)title);
      SendMessageW(filter,CB_SETCURSEL,0,0);
      control(L"BUTTON",L"全选",WS_TABSTOP,320,179,130,32,SelectAll);
      control(L"BUTTON",L"重试",WS_TABSTOP,0,0,100,34,Retry);
      control(L"BUTTON",L"打开文件",WS_TABSTOP,0,0,100,34,OpenFile);
      list=control(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SHOWSELALWAYS,16,181,910,270,Tasks);
      ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
      wchar_t const *titles[]={L"文件",L"状态",L"已接收 / 总大小",L"速度"};
      int widths[]={290,210,300,110};
      for(int i=0;i<4;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(titles[i]);c.cx=widths[i];ListView_InsertColumn(list,i,&c);}
      pause_button=control(L"BUTTON",L"暂停",WS_TABSTOP,16,465,112,34,Pause);
      resume_button=control(L"BUTTON",L"恢复",WS_TABSTOP,140,465,126,34,Resume);
      remove_button=control(L"BUTTON",L"移除任务",WS_TABSTOP,280,465,140,34,Remove);
      files_button=control(L"BUTTON",L"文件清单",WS_TABSTOP,430,465,140,34,Files);
      control(L"BUTTON",L"打开目录",WS_TABSTOP,580,465,140,34,Open);
      control(L"STATIC",L"",SS_CENTER|SS_CENTERIMAGE,0,0,0,0,Empty);
      auto detail_title=control(L"STATIC",L"任务详情",SS_CENTERIMAGE,0,0,0,0,Hint);
      SendMessageW(detail_title,WM_SETFONT,(WPARAM)heading_font,TRUE);
      details=control(L"EDIT",L"选择任务查看路径、错误和 SHA-256。",WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL,16,542,910,108,Details);
      refresh();SetTimer(h,1,200,nullptr);return 0;
    }
    if(msg==WM_CTLCOLORSTATIC){
      auto dc=(HDC)wp;SetBkColor(dc,GetSysColor(COLOR_WINDOW));
      SetTextColor(dc,GetSysColor((HWND(lp)==GetDlgItem(h,Title))?COLOR_HIGHLIGHT:COLOR_WINDOWTEXT));
      return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    if(msg==WM_SIZE){if(wp!=SIZE_MINIMIZED)layout(LOWORD(lp),HIWORD(lp));return 0;}
    if(msg==WM_GETMINMAXINFO){auto m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={px(740),px(600)};return 0;}
    if(msg==WM_NOTIFY) {
      auto header=reinterpret_cast<NMHDR*>(lp);
      if(header->hwndFrom==list&&header->code==LVN_KEYDOWN){auto key=reinterpret_cast<NMLVKEYDOWN*>(lp);if(key->wVKey=='A'&&(GetKeyState(VK_CONTROL)&0x8000))SendMessageW(h,WM_COMMAND,SelectAll,0);if(key->wVKey==VK_DELETE)SendMessageW(h,WM_COMMAND,Remove,0);return 0;}
      if(header->hwndFrom==list&&header->code==NM_DBLCLK){SendMessageW(h,WM_COMMAND,OpenFile,0);return 0;}
      if(header->hwndFrom==list&&header->code==LVN_ITEMCHANGED){auto change=reinterpret_cast<NMLISTVIEW*>(lp);if(details&&(change->uChanged&LVIF_STATE)&&((change->uNewState^change->uOldState)&LVIS_SELECTED))refresh();return 0;}
      if(header->hwndFrom==list&&header->code==NM_CUSTOMDRAW) {
        auto d=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
        if(d->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
        if(d->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)return CDRF_NOTIFYSUBITEMDRAW;
        if(d->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM)&&d->iSubItem==2&&d->nmcd.dwItemSpec<visible.size()) {
          auto &t=*visible[d->nmcd.dwItemSpec];RECT r{};ListView_GetSubItemRect(list,int(d->nmcd.dwItemSpec),2,LVIR_BOUNDS,&r);InflateRect(&r,-4,-3);
          auto bg=CreateSolidBrush(RGB(232,236,241));FillRect(d->nmcd.hdc,&r,bg);DeleteObject(bg);
          if(t.total){auto fill=r;fill.right=fill.left+LONG((r.right-r.left)*std::min(1.0,double(t.bytes)/t.total));auto b=CreateSolidBrush(t.status==3?RGB(83,184,130):RGB(115,180,240));FillRect(d->nmcd.hdc,&fill,b);DeleteObject(b);}
          std::wostringstream label;if(t.total)label<<std::fixed<<std::setprecision(1)<<100.0*t.bytes/t.total<<L"%  ";label<<amount(t.bytes)<<L" / "<<(t.total?amount(t.total):L"未知");
          SetBkMode(d->nmcd.hdc,TRANSPARENT);SetTextColor(d->nmcd.hdc,RGB(20,30,40));auto s=label.str();DrawTextW(d->nmcd.hdc,s.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);return CDRF_SKIPDEFAULT;
        }
      }
    }
    if(msg==WM_CONTEXTMENU&&(HWND)wp==list) {
      POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(pt.x==-1){GetCursorPos(&pt);}
      HMENU menu=CreatePopupMenu();
      for(auto pair: {std::pair<int,wchar_t const*>{Pause,L"暂停"},{Resume,L"恢复"},{Retry,L"重试"},{Remove,L"移除…"},{OpenFile,L"打开文件"},{Open,L"打开所在文件夹"},{Files,L"选择 / 查看文件"},{SelectAll,L"全选当前分类"}})
        AppendMenuW(menu,MF_STRING|(IsWindowEnabled(GetDlgItem(h,pair.first))?0:MF_GRAYED),pair.first,pair.second);
      int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,h,nullptr);DestroyMenu(menu);if(command)SendMessageW(h,WM_COMMAND,command,0);return 0;
    }
    if(msg==WM_TIMER){tick();return 0;}
    if(msg==WM_APP+1){if(!startup_url.empty()){auto url=startup_url;startup_url.clear();add_tasks(url);}return 0;}
    // Do UI work after the synchronous IPC sender has received its durable ack.
    // Foreground/accessibility hooks must never hold the native-message call.
    if(msg==WM_APP+2){refresh();start_next();if(IsIconic(h))ShowWindow(h,SW_RESTORE);SetForegroundWindow(h);return 0;}
    if(msg==WM_COPYDATA) {
      auto p=reinterpret_cast<COPYDATASTRUCT*>(lp);
      if(closing||storage_failed||!p||(p->dwData!=0x48425436&&p->dwData!=0x48425437&&p->dwData!=0x48425438&&p->dwData!=0x48425439)||p->cbData<4||p->cbData>131074||p->cbData%2)return 0;
      auto s=static_cast<wchar_t const*>(p->lpData);size_t n=p->cbData/2;
      if(s[n-1]!=0)return 0;
      if(p->dwData==0x48425439) {
        std::istringstream input(utf8(std::wstring(s,n-1)));boost::property_tree::ptree json;boost::property_tree::read_json(input,json);
        auto request=browser_session::parse(json);add_tasks(wide(request.url),wide(request.filename));if(storage_failed)return 0;
        tasks.back()->control.browser_session=std::move(request.session);return tasks.back()->id;
      }
      if(p->dwData==0x48425438) {
        std::wstring value(s,n-1);
        if(value.empty()||value.size()>9||value.find_first_not_of(L"0123456789")!=value.npos)return 0;
        auto id=std::stoul(value);
        for(auto &t:tasks)if(t->id==id) {
          if(t->removing)return 0;
          if(t->status==3)return fs::is_regular_file(fs::u8path(t->path))&&fs::file_size(fs::u8path(t->path))==t->total?2:0;
          if(t->status==4&&t->browser_authorization_needed)return 5;
          if(t->status==4||t->status==6)return 3;
          if(t->status==2)return 4;
          return 1;
        }
        return 0;
      }
      auto url=browser_bridge::normalize(std::wstring(s,n-1));
      add_tasks(url);if(storage_failed)return 0;
      return p->dwData==0x48425437?tasks.back()->id:1;
    }
    if(msg==WM_COMMAND) {
      int id=LOWORD(wp);auto t=selected();
      if(id==show_library){MessageBoxW(h,data_dir.c_str(),L"当前任务库位置",MB_OK);return 0;}
      if(id==browse_library){
        wchar_t path[32768]{};OPENFILENAMEW dlg{};dlg.lStructSize=sizeof(dlg);dlg.hwndOwner=h;
        dlg.lpstrFile=path;dlg.nMaxFile=32768;dlg.lpstrFilter=L"任务记录 (tasks.record)\0tasks.record\0";
        dlg.lpstrTitle=L"选择旧版 userdata 文件夹中的 tasks.record";
        dlg.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&dlg)){
          fs::path record(path);
          if(!libraries::same(record.filename(),L"tasks.record"))throw std::runtime_error("请选择 tasks.record");
          open_library(record.parent_path());
        }
        return 0;
      }
      if(id>=first_library&&size_t(id-first_library)<known_libraries.size()){open_library(known_libraries[id-first_library].path);return 0;}
      if(id==Add)add_tasks();
      if(id==Filter&&HIWORD(wp)==CBN_SELCHANGE){category=int(SendMessageW(GetDlgItem(h,Filter),CB_GETCURSEL,0,0));ListView_SetItemState(list,-1,0,LVIS_SELECTED);}
      if(id==SelectAll)ListView_SetItemState(list,-1,LVIS_SELECTED,LVIS_SELECTED);
      if(!closing&&!storage_failed&&(id==Pause||id==Resume||id==Retry)) {
        for(auto p:selection())if(!p->removing){
          if((id==Pause&&(p->status==0||p->status==1))||(id==Resume&&p->status==2)||(id==Retry&&(p->status==4||p->status==6)))
            task_service.action(p->id,id==Pause?"pause":id==Resume?"resume":"retry");
        }start_next();
      }
      if(id==Remove&&!closing&&!storage_failed) {
        auto chosen=selection();chosen.erase(std::remove_if(chosen.begin(),chosen.end(),[](Task*p){return p->removing;}),chosen.end());if(!chosen.empty()) {
          TASKDIALOG_BUTTON buttons[]={{201,L"仅移除任务\n保留下载文件、临时文件、恢复记录和缓存"},{202,L"同时删除下载文件\n仅将有创建归属记录且身份未变的文件及缓存送到回收站"}};
          TASKDIALOGCONFIG cfg{};cfg.cbSize=sizeof(cfg);cfg.hwndParent=h;cfg.dwFlags=TDF_USE_COMMAND_LINKS|TDF_ALLOW_DIALOG_CANCELLATION;cfg.dwCommonButtons=TDCBF_CANCEL_BUTTON;cfg.pszWindowTitle=L"移除下载任务";
          auto title=L"移除选中的 "+std::to_wstring(chosen.size())+L" 个任务？";cfg.pszMainInstruction=title.c_str();
          cfg.pszContent=L"运行中的任务会先停止并保存。仅影响选中任务；不删除整个文件夹。旧版未登记、被修改或额外放入的文件会阻止删除。无法可靠回收时停止，无永久删除选项。\n仅移除后没有自动找回或按文件名清理；未登记临时文件和空目录保留；同时删除成功后清理该任务的空锁文件。";
          std::wstring scope;for(auto p:chosen)scope+=L"#"+std::to_wstring(p->id)+L"  "+wide(p->path)+L"\n";cfg.pszExpandedInformation=scope.c_str();cfg.pszExpandedControlText=L"查看选中任务路径";
          cfg.cButtons=2;cfg.pButtons=buttons;cfg.nDefaultButton=IDCANCEL;int result=0;
          if(SUCCEEDED(TaskDialogIndirect(&cfg,&result,nullptr,nullptr))&&(result==201||result==202)) {
            for(auto p:chosen)task_service.request_remove(p->id,result==202);
            remove_stopped();
          }
        }
      }
      if(id==Files&&t&&t->status!=1&&!closing)choose_files(*t);
      if((id==Open||id==OpenFile)&&t) {
        auto path=fs::u8path(t->path);bool magnet=t->url.rfind("magnet:?",0)==0;
        if(id==OpenFile){if(t->status!=3)throw std::runtime_error("只能打开已完成的文件");if(magnet){choose_files(*t);return 0;}}
        else if(magnet&&fs::exists(path/L"files"))path/=L"files";else if(!magnet)path=path.parent_path();
        if(!fs::exists(path))throw std::runtime_error("文件或文件夹已不存在");
        if((INT_PTR)ShellExecuteW(h,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error("Windows 无法打开该文件或文件夹");
      }
      if(id==Browse){BROWSEINFOW b{};b.hwndOwner=h;b.lpszTitle=L"选择下载目录";b.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
        auto pid=SHBrowseForFolderW(&b);if(pid){wchar_t path[MAX_PATH];if(SHGetPathFromIDListW(pid,path))SetWindowTextW(destination,path);CoTaskMemFree(pid);}}
      refresh();return 0;
    }
    if(msg==WM_CLOSE) {
      closing=true;EnableWindow(add_button,FALSE);
      SetWindowTextW(h,L"HandmadeBT — 正在暂停并保存，请稍候…");
      task_service.begin_shutdown();tick();return 0;
    }
    if(msg==WM_DESTROY){KillTimer(h,1);PostQuitMessage(0);return 0;}
  } catch(std::exception const &e){message(e.what());}
  return DefWindowProcW(h,msg,wp,lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
  try {
    SetProcessDPIAware();auto dc=GetDC(nullptr);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(nullptr,dc);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&cc);
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);
    auto home=browser_bridge::home();
    bool explicit_data=false,choose_library=false;
    int argc;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1&&std::wstring(argv[1])==L"--protocol") {
      if(argc!=3)throw std::runtime_error("invalid protocol invocation");
      startup_url=browser_bridge::normalize(argv[2]);
    }
    else for(int i=1;i<argc;i+=2) {
      if(i+1>=argc)throw std::runtime_error("missing command argument");
      if(std::wstring(argv[i])==L"--data-dir"||std::wstring(argv[i])==L"--use-library"){
        explicit_data=true;choose_library=std::wstring(argv[i])==L"--use-library";
        data_dir=fs::absolute(argv[i+1]).lexically_normal();
        if(choose_library&&!fs::is_regular_file(data_dir/L"tasks.record"))throw std::runtime_error("找不到该任务库的 tasks.record");
      }
      else if(std::wstring(argv[i])==L"--open"&&startup_url.empty())startup_url=browser_bridge::normalize(argv[i+1]);
      else throw std::runtime_error("usage: handmadebt.exe [--data-dir DIRECTORY] [--open URL]");
    }
    if(!explicit_data){
#ifdef HBT_VERSIONED_DESKTOP
      data_dir=libraries::initial(home);
#else
      data_dir=home/L"userdata";
#endif
    }
    auto class_name=browser_bridge::window_class(data_dir);
    if(auto existing=FindWindowW(class_name.c_str(),nullptr)) {
      DWORD pid=0;GetWindowThreadProcessId(existing,&pid);
      HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
      wchar_t running[32768]{};DWORD length=32768;
      bool same_exe=process&&QueryFullProcessImageNameW(process,0,running,&length)&&libraries::same(running,exe);
      if(process)CloseHandle(process);
      if(!same_exe)throw std::runtime_error("该任务库正由另一个版本使用。请先关闭旧版 HandmadeBT，再打开新版；任务和断点会保留。");
      if(choose_library)libraries::remember(home,data_dir);
      if(!startup_url.empty()&&!browser_bridge::send(existing,startup_url))throw std::runtime_error("existing window did not accept URL");
      ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);LocalFree(argv);return 0;
    }
    LocalFree(argv);fs::create_directories(data_dir);
    instance_lock=std::make_unique<core::TaskLock>(data_dir/L"ui.lock");load_tasks();
#ifdef HBT_VERSIONED_DESKTOP
    if(!explicit_data||choose_library)libraries::remember(home,data_dir);
#else
    if(choose_library)libraries::remember(home,data_dir);
#endif
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=class_name.c_str();
    wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    window=CreateWindowW(wc.lpszClassName,version,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                         CW_USEDEFAULT,CW_USEDEFAULT,px(1040),px(740),nullptr,nullptr,instance,nullptr);
    if(!window)throw std::runtime_error("cannot create window");
    ShowWindow(window,show);UpdateWindow(window);
    if(!startup_url.empty())PostMessageW(window,WM_APP+1,0,0);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    DeleteObject(title_font);DeleteObject(heading_font);DeleteObject(font);CoUninitialize();return 0;
  }catch(std::exception const &e){MessageBoxW(nullptr,wide(e.what()).c_str(),version,MB_OK|MB_ICONERROR);return 1;}
}

