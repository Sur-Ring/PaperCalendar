#pragma once
#include <QAbstractNativeEventFilter>
#include <QtWidgets>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
// Opt-in experiment: host this application's HWND in the desktop icon view.
// Does not modify Explorer styles, force wallpaper creation, or change alpha
// APIs.
class DesktopAttachment : public QObject, public QAbstractNativeEventFilter {
  Q_OBJECT
public:
  explicit DesktopAttachment(QWidget *window);
  ~DesktopAttachment() override;
  bool attach();
  void start();
  void detach();
  bool isAttached() const { return attached; }
  QString diagnostic() const { return message; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
#else
  bool nativeEventFilter(const QByteArray &, void *, long *) override;
#endif
signals:
  void recoveryRequired();

private:
  QWidget *window;
  std::unique_ptr<QWidget> receiver;
  QTimer monitor, retry;
  std::unique_ptr<QWindow> foreignHost;
  QString message;
  bool attached = false, stopping = false;
  int retries = 0;
#ifdef Q_OS_WIN
  HWND host = nullptr, child = nullptr, oldParent = nullptr;
  LONG_PTR oldStyle = 0;
  UINT taskbarCreated = 0;
  DWORD hostPid = 0;
  static HWND findIconView();
  static HWND findHost();
  void hostChanged();
#endif
};
