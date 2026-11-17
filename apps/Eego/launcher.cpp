#define UNICODE
#define _UNICODE
#include <windows.h>
#include <string>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR args,int){
 wchar_t self[32768];DWORD len=GetModuleFileNameW(nullptr,self,32768);if(!len||len>=32768)return 3;
 std::wstring root(self,len);root.resize(root.find_last_of(L"\\/")+1);
 std::wstring home=root+L"dist\\Eego\\";
 for(auto name:{L"HandmadeBTQuick.exe",L"core\\task_service_host_r3.exe",L"Qt6Core.dll",L"platforms\\qwindows.dll",L"library-path.txt"}){
  DWORD a=GetFileAttributesW((home+name).c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_DIRECTORY)){
   MessageBoxW(nullptr,(L"Required application file is missing:\n"+home+name+L"\nNo alternate library was opened.").c_str(),L"Eego - cannot start",MB_OK|MB_ICONERROR);return 2;
  }
 }
 std::wstring exe=home+L"HandmadeBTQuick.exe",cmd=L"\""+exe+L"\" "+args;
 STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
 if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,home.c_str(),&si,&pi)){
  MessageBoxW(nullptr,L"Cannot start Eego. Check the application runtime files.",L"Eego",MB_OK|MB_ICONERROR);return 3;
 }
 CloseHandle(pi.hThread);WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=3;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);return int(code);
}
