// Integration harness includes the production controller and real native controls.
// Only accepts a freshly created isolated directory supplied by the Python runner.
#define wWinMain gui_entry_unused
#include "../apps/HandmadeBT/gui.cpp"
#undef wWinMain
#include <iostream>
void require(bool b,char const*what){if(!b)throw std::runtime_error(what);std::cout<<"PASS "<<what<<std::endl;}
void await_state(std::function<bool()> test,int seconds=40){auto end=GetTickCount64()+seconds*1000;while(!test()){if(GetTickCount64()>end)throw std::runtime_error("timeout waiting for task state");tick();Sleep(20);}refresh();}
void all(){SendMessageW(window,WM_COMMAND,SelectAll,0);}
void filter(int c){SendMessageW(GetDlgItem(window,Filter),CB_SETCURSEL,c,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(Filter,CBN_SELCHANGE),0);}
int wmain(int argc,wchar_t**argv){
 try {
  if(argc==3&&std::wstring(argv[1])==L"--recycle-case-check") {
    auto dir=fs::absolute(argv[2]);core::reject_bt_reparse(dir);
    auto target=dir/(L".hbt-delete-check-"+std::to_wstring(GetTickCount64())+L".tmp");
    core::FileStore::prepare(target,4,false);core::FileStore::write(target,0,"test",4);core::FileStore::flush(target);
    auto lower=target.wstring();for(auto &c:lower)if(c>=L'A'&&c<=L'Z')c+=32;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);managed::recycle({fs::path(lower)},nullptr);
    require(!fs::exists(target),"real recycle succeeds with case-varied existing download directory");CoUninitialize();return 0;
  }
  if(argc==3&&std::wstring(argv[1])==L"--inventory-check") {
    data_dir=fs::absolute(argv[2]);load_tasks();
    for(auto&t:tasks){auto plan=t->inventory.plan();std::cout<<"task "<<t->id<<" deletion plan validated: "<<plan.size()<<" files"<<std::endl;}
    return 0;
  }
  if(argc==3&&std::wstring(argv[1])==L"--reopen-check") {
    data_dir=fs::absolute(argv[2]);load_tasks();bool complete=false,paused=false,failed=false;
    for(auto&t:tasks){complete|=t->status==3;paused|=t->status==2;failed|=t->status==4;}
    require(complete&&paused&&failed,"new process reloads completed paused failed and verifies completed content");return 0;
  }
  if(argc!=4&&argc!=5)return 2;data_dir=fs::absolute(argv[1]);if(fs::exists(data_dir))throw std::runtime_error("fresh directory required");fs::create_directories(data_dir);
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&cc);
  WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=proc;wc.lpszClassName=L"HBT08Integration";RegisterClassW(&wc);
  window=CreateWindowW(wc.lpszClassName,L"isolated acceptance",WS_OVERLAPPEDWINDOW,0,0,1100,760,nullptr,nullptr,wc.hInstance,nullptr);KillTimer(window,1);
  std::wstring base=argv[2],hash=argv[3];
  SetWindowTextW(links,(base+L"/slow.bin | "+hash+L"\n"+base+L"/range.bin | "+hash+L"\n"+base+L"/missing.bin").c_str());add_tasks();
  await_state([]{return tasks[0]->control.received>50000;});all();SendMessageW(window,WM_COMMAND,Pause,0);
  await_state([]{return tasks[0]->status!=1;});require(tasks[0]->status==2&&tasks[1]->status==2&&tasks[2]->status==2,"batch pause active and waiting");
  require(selection().size()==3,"native multiselection and select all");
  persist();visible.clear();ListView_DeleteAllItems(list);task_service.clear_stopped_tasks();load_tasks();refresh();require(tasks.size()==3&&tasks[0]->status==2,"reload paused state and inventory");
  all();SendMessageW(window,WM_COMMAND,Resume,0);await_state([]{return tasks[0]->status==3&&tasks[1]->status==3&&tasks[2]->status==4;},90);
  require(file_hash(*tasks[0])==utf8(hash)&&file_hash(*tasks[1])==utf8(hash),"both completed contents SHA256 verified");
  filter(2);require(visible.size()==2,"completed classification");all();require(selection().size()==2,"select all limited to visible category");
  filter(1);require(visible.size()==1&&visible[0]->status==4&&selection().empty(),"unfinished includes failure and clears hidden selection");all();SendMessageW(window,WM_COMMAND,Retry,0);await_state([]{return tasks[2]->status==4;});require(tasks[2]->status==4,"batch retry real HTTP failure");
  persist();visible.clear();ListView_DeleteAllItems(list);task_service.clear_stopped_tasks();load_tasks();refresh();require(tasks[0]->status==3&&tasks[2]->status==4,"reload completed and failed statuses");
  auto kept=fs::u8path(tasks[0]->path);tasks[0]->removing=true;remove_stopped();require(fs::exists(kept)&&tasks.size()==2,"remove-only retains completed file");
  auto deleted=fs::u8path(tasks[0]->path);auto plan=tasks[0]->inventory.plan();require(plan.size()==1,"only registered completed output in deletion plan");
  tasks[0]->removing=true;tasks[0]->delete_files=true;remove_stopped();require(!fs::exists(deleted)&&tasks.size()==1,"completed output actually recycled");
  require(!fs::exists(fs::path(deleted.wstring()+L".hbt-lock")),"completed delete also removes task lock");
  require(fs::exists(fs::path(kept.wstring()+L".hbt-lock")),"remove-only retains task lock");
  filter(0);SetWindowTextW(links,(base+L"/slow.bin | "+hash+L"\n"+base+L"/slow.bin | "+hash).c_str());add_tasks();await_state([]{return tasks[1]->control.received>50000;});
  auto partial=fs::u8path(tasks[1]->path+".part");auto partial2=fs::u8path(tasks[2]->path+".part");
  tasks[1]->removing=true;tasks[1]->delete_files=true;tasks[1]->control.pause=true;tasks[2]->removing=true;remove_stopped();
  await_state([]{return tasks.size()==1;});require(!fs::exists(partial)&&!fs::exists(partial2),"running recycle waits for worker; waiting removal never starts");
  auto partial_lock=partial;partial_lock.replace_extension(L".hbt-lock");
  require(!fs::exists(partial_lock),"running delete cleans lock after worker stops");
  managed::Inventory inv;inv.target=data_dir/L"guard.bin";inv.record=data_dir/L"guard.record";inv.binding="isolated guard";
  {std::ofstream f(inv.target);f<<"user-owned";}bool refused=false;try{inv.plan();}catch(...){refused=true;}require(refused&&fs::file_size(inv.target)==10,"unregistered legacy user file refused");
  inv.event(inv.target,{});inv.save();{std::ofstream f(inv.target,std::ios::app);f<<"changed";}refused=false;try{inv.plan();}catch(...){refused=true;}require(refused&&fs::exists(inv.target),"modified registered file refused");
  inv.begin();inv.save();refused=false;try{inv.plan();}catch(...){refused=true;}require(refused,"retry cannot bless user edits into deletion inventory");
  auto peer=data_dir/L"other-task.bin";{std::ofstream f(peer);f<<"other-task";}inv.entries.clear();refused=false;try{inv.plan();}catch(...){refused=true;}require(refused&&fs::file_size(peer)==10,"other task unaffected");
  auto link=data_dir/L"hardlink.bin";require(CreateHardLinkW(link.c_str(),peer.c_str(),nullptr),"hardlink fixture created");refused=false;try{managed::identity(link);}catch(...){refused=true;}require(refused&&fs::exists(peer),"hardlinks refused");
  managed::Inventory replace;replace.target=data_dir/L"replace.bin";replace.record=data_dir/L"replace.record";replace.binding="replacement guard";
  {std::ofstream f(replace.target);f<<"original";}replace.event(replace.target,{});replace.save();
  fs::rename(replace.target,data_dir/L"original-preserved.bin");{std::ofstream f(replace.target);f<<"new user file";}
  refused=false;try{replace.plan();}catch(...){refused=true;}require(refused&&fs::file_size(replace.target)==13,"replaced file identity refused");
  auto locked=data_dir/L"locked.bin";{std::ofstream f(locked);f<<"keep locked";}
  auto handle=CreateFileW(locked.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  require(handle!=INVALID_HANDLE_VALUE,"locked deletion fixture opened");refused=false;try{managed::recycle({locked},window);}catch(...){refused=true;}CloseHandle(handle);
  require(refused&&fs::file_size(locked)==11,"actual Shell recycle failure retains locked file");
  auto sink=new managed::Sink;require(FAILED(sink->PreDeleteItem(0,nullptr)),"permanent deletion fallback vetoed");sink->Release();
  require(fs::exists(kept),"removed historical output still retained after later cleanup");
  if(argc==5) {
    SetWindowTextW(links,argv[4]);add_tasks();await_state([]{return tasks.back()->status==5;});
    auto &bt=*tasks.back();apply_selection(bt,{1,3});start_next();await_state([&]{return bt.control.received>20000;});
    bt.control.pause=true;await_state([&]{return bt.status==2;});require(!bt.inventory.plan().empty(),"BT pause inventory includes actual files and boundary cache");
    bt.status=0;start_next();await_state([&]{return bt.status==3;},90);require(file_hash(bt)==bt.digest,"BT selected content piece and per-file hashes verified");
    auto read_bytes=[](fs::path const&p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
    require(read_bytes(fs::u8path(bt.path)/L"files/bundle/b.bin")==read_bytes(data_dir.parent_path()/L"seed/bundle/b.bin"),"BT selected output byte-for-byte matches independent source");
    auto sentinel=fs::u8path(bt.path)/L"files/user-note.txt";{std::ofstream f(sentinel);f<<"user data";}
    refused=false;try{bt.inventory.plan();}catch(...){refused=true;}require(refused&&fs::exists(sentinel),"BT extra user file blocks cleanup");
    fs::rename(sentinel,data_dir/L"retained-user-note.txt");auto root=fs::u8path(bt.path);bt.removing=true;bt.delete_files=true;remove_stopped();
    require(!fs::exists(root/L"files.record")&&!fs::exists(root/L"metadata.record")&&fs::exists(data_dir/L"retained-user-note.txt"),"BT files metadata resume and cache recycled without deleting user file");
    require(!fs::exists(root/L"magnet.lock"),"BT delete also removes magnet lock");
  }
  SetWindowTextW(links,(base+L"/range.bin | "+hash).c_str());add_tasks();await_state([]{return tasks.back()->status==3;});
  SetWindowTextW(links,(base+L"/slow.bin | "+hash).c_str());add_tasks();await_state([]{return tasks.back()->control.received>50000;});
  auto retained_part=fs::u8path(tasks.back()->path+".part"),retained_resume=fs::u8path(tasks.back()->path+".resume");auto before=tasks.size();
  tasks.back()->removing=true;tasks.back()->control.pause=true;await_state([&]{return tasks.size()==before-1;});
  require(fs::exists(retained_part)&&fs::exists(retained_resume),"running remove-only keeps partial and resume after worker stops");
  SetWindowTextW(links,(base+L"/slow.bin | "+hash).c_str());add_tasks();tasks.back()->control.pause=true;await_state([]{return tasks.back()->status==2;});
  persist();DestroyWindow(window);DeleteObject(font);CoUninitialize();std::cout<<"ALL PASS"<<std::endl;return 0;
 }catch(std::exception const&e){std::cerr<<e.what()<<std::endl;for(auto&t:tasks)if(t->worker.joinable()){t->control.pause=true;t->worker.join();}core::FileStore::created_or_moved={};return 1;}
}
