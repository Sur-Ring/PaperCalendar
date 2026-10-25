#include "DesktopController.h"
#include <QDebug>
#ifdef Q_OS_WIN
namespace {
bool isDesktop(HWND h){
 if(!h)return false;wchar_t name[128]={};GetClassNameW(h,name,128);QString cls=QString::fromWCharArray(name);
 return cls=="Progman"||cls=="WorkerW"||cls=="SHELLDLL_DefView";
}
}
DesktopController *DesktopController::instance=nullptr;
void CALLBACK DesktopController::callback(HWINEVENTHOOK,DWORD event,HWND hwnd,LONG object,LONG child,DWORD,DWORD){
 if(instance&&hwnd&&event==EVENT_SYSTEM_FOREGROUND&&object==OBJID_WINDOW&&child==CHILDID_SELF)instance->foregroundChanged();
}
bool DesktopController::desktopCovers(HWND target) const {
 RECT bounds;if(!GetWindowRect(target,&bounds))return false;
 // Walk only higher windows, and ignore invisible/off-monitor shell hosts.
 // The bound prevents a changing Z-order from creating an endless traversal.
 HWND h=GetWindow(target,GW_HWNDPREV);
 for(int count=0;h&&h!=target&&count<512;count++,h=GetWindow(h,GW_HWNDPREV)){
  if(!IsWindowVisible(h)||!isDesktop(h))continue;
  RECT desktop,overlap;if(GetWindowRect(h,&desktop)&&IntersectRect(&overlap,&bounds,&desktop))return true;
 }
 return false;
}
#endif
DesktopController::DesktopController(QWidget *w):QObject(w),window(w){
 settle.setSingleShot(true);settle.setInterval(0);
 verify.setSingleShot(true);verify.setInterval(180);
 release.setSingleShot(true);release.setInterval(50);
 connect(&settle,&QTimer::timeout,this,&DesktopController::revealIfNeeded);
 connect(&verify,&QTimer::timeout,this,&DesktopController::revealIfNeeded);
 connect(&release,&QTimer::timeout,this,&DesktopController::demote);
#ifdef Q_OS_WIN
 instance=this;
 hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,callback,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
 if(!hook)qWarning()<<"Cannot install desktop foreground hook"<<GetLastError();
#endif
 QTimer::singleShot(0,this,[this]{foregroundChanged();});
}
DesktopController::~DesktopController(){
 settle.stop();verify.stop();release.stop();
#ifdef Q_OS_WIN
 if(hook)UnhookWinEvent(hook);
 if(instance==this)instance=nullptr;
#endif
 demote();
}
bool DesktopController::desktopForeground() const {
#ifdef Q_OS_WIN
 return isDesktop(GetForegroundWindow());
#else
 return false;
#endif
}
void DesktopController::revealIfNeeded(){
#ifdef Q_OS_WIN
 if(window->property("desktopAttached").toBool()||temporaryTopmost||!desktopForeground()||!window->isVisible())return;
 HWND h=reinterpret_cast<HWND>(window->winId());if(!IsWindow(h))return;
 bool restore=IsIconic(h)||!IsWindowVisible(h);
 if(!restore&&!desktopCovers(h))return;
 // Calling ShowWindow on an already visible layered window is unnecessary.
 if(restore)ShowWindow(h,SW_SHOWNOACTIVATE);
 if(SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)){
  temporaryTopmost=true;release.start();
 }else qWarning()<<"Cannot reveal desktop companion"<<GetLastError();
#endif
}
void DesktopController::demote(){
 if(!temporaryTopmost)return;
#ifdef Q_OS_WIN
 if(window->testAttribute(Qt::WA_WState_Created)){
  HWND h=reinterpret_cast<HWND>(window->winId());
  if(IsWindow(h)&&!SetWindowPos(h,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)){
   qWarning()<<"Cannot release temporary topmost"<<GetLastError();release.start();return;
  }
 }
#endif
 temporaryTopmost=false;
}
void DesktopController::foregroundChanged(){
 if(window->property("desktopAttached").toBool()){settle.stop();verify.stop();return;}
 if(desktopForeground()){
  settle.start(); // Immediate, coalesced desktop return.
  verify.start(); // Recheck after the Win+D animation, without an unconditional pulse.
 }else{
  settle.stop();verify.stop();release.stop();demote();
 }
}
