#include "DesktopAttachment.h"
#include <QJsonObject>
#ifdef Q_OS_WIN
namespace {
QString cls(HWND h) {
  wchar_t text[128] = {};
  GetClassNameW(h, text, 128);
  return QString::fromWCharArray(text);
}
bool sameDpi(HWND a, HWND b) {
  using Get = HANDLE(WINAPI *)(HWND);
  using Equal = BOOL(WINAPI *)(HANDLE, HANDLE);
  auto user = GetModuleHandleW(L"user32.dll");
  auto get = reinterpret_cast<Get>(
      GetProcAddress(user, "GetWindowDpiAwarenessContext"));
  auto equal = reinterpret_cast<Equal>(
      GetProcAddress(user, "AreDpiAwarenessContextsEqual"));
  return get && equal && equal(get(a), get(b));
}
bool style(HWND h, LONG_PTR value) {
  SetLastError(0);
  auto previous = SetWindowLongPtrW(h, GWL_STYLE, value);
  return previous || GetLastError() == 0;
}
} // namespace
HWND DesktopAttachment::findIconView() {
  HWND progman = FindWindowW(L"Progman", nullptr);
  HWND found = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
  if (progman && found && IsWindowVisible(found))
    return found;
  struct Search {
    HWND result = nullptr;
  };
  Search search;
  EnumWindows(
      [](HWND top, LPARAM data) -> BOOL {
        auto s = reinterpret_cast<Search *>(data);
        if (cls(top) != "Progman" && cls(top) != "WorkerW")
          return TRUE;
        if (auto view =
                FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {
          if (IsWindowVisible(view)) {
            s->result = view;
            return FALSE;
          }
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.result;
}
HWND DesktopAttachment::findHost(){
 HWND view=findIconView();if(!view)return nullptr;
 HWND root=GetAncestor(view,GA_ROOT);
 // Modern DefView is itself layered. Use a sibling of it rather than nesting
 // another layered surface inside the shell's redirected icon view.
 if(cls(root)=="Progman"&&(GetWindowLongPtrW(root,GWL_EXSTYLE)&WS_EX_NOREDIRECTIONBITMAP))return root;
 return view;
}
#endif
DesktopAttachment::DesktopAttachment(QWidget *w) : QObject(w), window(w) {
  receiver =
      std::make_unique<QWidget>(nullptr, Qt::Tool | Qt::FramelessWindowHint);
  receiver->setAttribute(Qt::WA_QuitOnClose, false);
  receiver->setWindowTitle("PaperCalendar desktop host monitor");
  receiver->winId();
  qApp->installNativeEventFilter(this);
  monitor.setInterval(1000);
  retry.setSingleShot(true);
  retry.setInterval(750);
  connect(&monitor, &QTimer::timeout, this, [this] {
#ifdef Q_OS_WIN
    if (!attached)
      return;
    DWORD pid = 0;
    if (IsWindow(host))
      GetWindowThreadProcessId(host, &pid);
    if (!IsWindow(host) || pid != hostPid || !IsWindow(child) ||
        GetParent(child) != host || findHost() != host){hostChanged();return;}
    if(cls(host)=="Progman")for(HWND above=GetWindow(child,GW_HWNDPREV);above;above=GetWindow(above,GW_HWNDPREV)){
      if(cls(above)=="SHELLDLL_DefView"&&IsWindowVisible(above)){SetWindowPos(child,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);break;}
    }
#endif
  });
  connect(&retry, &QTimer::timeout, this, [this] {
    if (stopping)
      return;
    if (attach()) {
      retries = 0;
      return;
    }
    if (++retries < 3)
      retry.start();
    else {
      message = QStringLiteral("桌面挂载恢复失败，已回退：") + message;
      qWarning().noquote() << message;
    }
  });
#ifdef Q_OS_WIN
  taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
#endif
}
DesktopAttachment::~DesktopAttachment() {
  stopping = true;
  monitor.stop();
  retry.stop();
  qApp->removeNativeEventFilter(this);
  detach();
}
void DesktopAttachment::start() {
  if (!attach()) {
    qWarning().noquote() << "Desktop attachment deferred:" << message;
    retries = 0;
    retry.start();
  }
}
bool DesktopAttachment::attach() {
#ifdef Q_OS_WIN
  if (attached)
    return true;
  if (QGuiApplication::platformName() != "windows") {
    message = "Requires native Windows platform";
    return false;
  }
  HWND candidate = findHost();
  if (!candidate || !IsWindowVisible(candidate)) {
    message = "No visible desktop icon view";
    return false;
  }
  HWND handle = reinterpret_cast<HWND>(window->winId());
  if (!IsWindow(handle)) {
    message = "Application HWND is invalid";
    return false;
  }
  if (!sameDpi(handle, candidate)) {
    message = "DPI awareness contexts differ; refused unsafe SetParent";
    return false;
  }
  if (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) {
    message = "Temporary topmost is still active";
    return false;
  }
  RECT rectangle;
  if (!GetWindowRect(handle, &rectangle)) {
    message = "Cannot read application bounds";
    return false;
  }
  oldStyle = GetWindowLongPtrW(handle, GWL_STYLE);
  oldParent = GetParent(handle);
  child = handle;
  host = candidate;
  GetWindowThreadProcessId(host, &hostPid);
  foreignHost.reset(QWindow::fromWinId(WId(reinterpret_cast<quintptr>(host))));
  if(!foreignHost){message="Qt cannot wrap desktop host";child=nullptr;host=nullptr;return false;}
  LONG_PTR extended=GetWindowLongPtrW(child,GWL_EXSTYLE);
  SetWindowLongPtrW(child,GWL_EXSTYLE,extended&~WS_EX_LAYERED);
  window->windowHandle()->setParent(foreignHost.get());
  child=reinterpret_cast<HWND>(window->winId());
  SetWindowLongPtrW(child,GWL_EXSTYLE,extended|WS_EX_LAYERED);
  DWORD error=GetLastError();
  if(GetParent(child)!=host){
    window->windowHandle()->setParent(nullptr);foreignHost.reset();style(child,oldStyle);message=QString("Qt reparent failed: %1").arg(error);child=nullptr;host=nullptr;return false;
  }
  POINT point = {rectangle.left, rectangle.top};
  MapWindowPoints(nullptr, host, &point, 1);
  if (!SetWindowPos(child, HWND_TOP, point.x, point.y,
                    rectangle.right - rectangle.left,
                    rectangle.bottom - rectangle.top,
                    SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW)) {
    message = QString("Child positioning failed: %1").arg(GetLastError());
    window->windowHandle()->setParent(nullptr);foreignHost.reset();
    style(child, oldStyle);
    SetWindowPos(child, nullptr, rectangle.left, rectangle.top,
                 rectangle.right - rectangle.left,
                 rectangle.bottom - rectangle.top,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
    child = nullptr;
    host = nullptr;
    return false;
  }
  attached = true;
  window->setProperty("desktopAttached", true);
  window->setProperty("desktopHostClass", cls(host));
  message =
      QString("Attached to %1; WS_CHILD=1; layered=%2; compatible DPI=1")
          .arg(cls(host))
          .arg(bool(GetWindowLongPtrW(child, GWL_EXSTYLE) & WS_EX_LAYERED));
  qInfo().noquote() << message;
  qInfo()<<"Qt geometry after SetParent"<<window->geometry()<<"frame"<<window->frameGeometry()<<"QWindow"<<window->windowHandle()->geometry();
  window->update();QTimer::singleShot(0,window,[this]{if(attached){window->repaint();qInfo()<<"Full repaint submitted after SetParent";}});
  monitor.start();
  return true;
#else
  message = "Windows desktop attachment is unavailable";
  return false;
#endif
}
void DesktopAttachment::detach() {
  monitor.stop();
#ifdef Q_OS_WIN
  if (attached && IsWindow(child)) {
    RECT rectangle;
    bool bounds = GetWindowRect(child, &rectangle);
    window->windowHandle()->setParent(nullptr);
    if(GetParent(child)==host){qWarning()<<"Qt detach did not release desktop parent";if(!stopping)monitor.start();return;}
    foreignHost.reset();
    style(child, oldStyle);
    if (bounds)
      SetWindowPos(child, nullptr, rectangle.left, rectangle.top,
                   rectangle.right - rectangle.left,
                   rectangle.bottom - rectangle.top,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  if(foreignHost){if(window->windowHandle())window->windowHandle()->setParent(nullptr);foreignHost.reset();}
  host = nullptr;
  child = nullptr;
#endif
  attached = false;
  window->setProperty("desktopAttached", false);
}
#ifdef Q_OS_WIN
void DesktopAttachment::hostChanged() {
  if (stopping || !attached)
    return;
  bool destroyed = !IsWindow(child);
  detach();
  if (destroyed)
    emit recoveryRequired();
  retries = 0;
  retry.start();
}
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool DesktopAttachment::nativeEventFilter(const QByteArray &, void *message,
                                          qintptr *) {
#else
bool DesktopAttachment::nativeEventFilter(const QByteArray &, void *message,
                                          long *) {
#endif
#ifdef Q_OS_WIN
  auto msg = static_cast<MSG *>(message);
  if (taskbarCreated && msg->message == taskbarCreated && attached)
    QTimer::singleShot(0, this, [this] { hostChanged(); });
#else
  Q_UNUSED(message);
#endif
  return false;
}
