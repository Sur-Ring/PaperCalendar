// Read-only shell topology diagnostic. No messages, reparenting, or window changes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <iostream>
#include <string>
#include <set>
static std::set<HWND> printed;
static std::wstring className(HWND h){wchar_t cls[256]={};GetClassNameW(h,cls,256);return cls;}
static bool shellClass(const std::wstring &s){return s==L"Progman"||s==L"WorkerW"||s==L"SHELLDLL_DefView"||s==L"SysListView32";}
static void print(HWND h){
 if(!printed.insert(h).second)return;
 DWORD pid=0,cloaked=0;GetWindowThreadProcessId(h,&pid);RECT r={};GetWindowRect(h,&r);HRESULT status=DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof cloaked);
 using DpiFn=UINT(WINAPI*)(HWND);using CtxFn=HANDLE(WINAPI*)(HWND);using AwarenessFn=int(WINAPI*)(HANDLE);
 HMODULE user=GetModuleHandleW(L"user32.dll");auto dpi=reinterpret_cast<DpiFn>(GetProcAddress(user,"GetDpiForWindow"));auto ctx=reinterpret_cast<CtxFn>(GetProcAddress(user,"GetWindowDpiAwarenessContext"));auto awareness=reinterpret_cast<AwarenessFn>(GetProcAddress(user,"GetAwarenessFromDpiAwarenessContext"));
 std::wcout<<L"class="<<className(h)<<L" hwnd="<<h<<L" parent="<<GetParent(h)<<L" pid="<<pid<<L" visible="<<IsWindowVisible(h)<<L" style=0x"<<std::hex<<GetWindowLongPtrW(h,GWL_STYLE)<<L" exstyle=0x"<<GetWindowLongPtrW(h,GWL_EXSTYLE)<<std::dec<<L" rect="<<r.left<<L","<<r.top<<L","<<r.right<<L","<<r.bottom<<L" dpi="<<(dpi?dpi(h):0)<<L" awareness="<<((ctx&&awareness)?awareness(ctx(h)):-1)<<L" cloaked="<<(SUCCEEDED(status)?int(cloaked):-1)<<L"\n";
}
static BOOL CALLBACK child(HWND h,LPARAM){if(shellClass(className(h)))print(h);return TRUE;}
static BOOL CALLBACK root(HWND h,LPARAM){auto cls=className(h);if(cls==L"Progman"||cls==L"WorkerW"){print(h);EnumChildWindows(h,child,0);}return TRUE;}
int main(){
 using SetContextFn=BOOL(WINAPI*)(HANDLE);auto setContext=reinterpret_cast<SetContextFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));if(setContext)setContext(reinterpret_cast<HANDLE>(-4));
 EnumWindows(root,0);if(printed.empty()){std::cout<<"No shell windows visible to this process.\n";return 2;}return 0;}
