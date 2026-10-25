#pragma once
#include <QObject>
#include <QTimer>
#include <QWidget>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
// Retains the original topmost pulse, but only when the desktop actually covers
// the window. All native operations resolve the current HWND.
class DesktopController : public QObject {
  Q_OBJECT
public:
  explicit DesktopController(QWidget *window);
  ~DesktopController() override;
private:
  QWidget *window;
  QTimer settle, verify, release;
  bool temporaryTopmost=false;
  bool desktopForeground() const;
  void foregroundChanged();
  void revealIfNeeded();
  void demote();
#ifdef Q_OS_WIN
  bool desktopCovers(HWND target) const;
  HWINEVENTHOOK hook=nullptr;
  static DesktopController *instance;
  static void CALLBACK callback(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD);
#endif
};
