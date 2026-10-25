#include "Calendar/CalendarWindow.h"
#include "Calendar/DesktopAttachment.h"
#include <QApplication>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScreen>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("PaperCalendar");
  bool experiment = app.arguments().contains("--desktop-host");
#ifdef PAPERCALENDAR_EXPERIMENTAL_DESKTOP_HOST
  experiment = true;
#endif
  QCoreApplication::setApplicationName(experiment ? "PaperCalendarWorkerW"
                                                  : "PaperCalendar");
  if (experiment)
    app.setQuitOnLastWindowClosed(false);
  CalendarWindow window;
  window.setProperty("desktopExperiment", experiment);
  if(experiment)if(auto title=window.findChild<QLabel*>("dragHeader"))title->setText(QStringLiteral("会议时间轴 · 实验"));
  if (experiment)
    window.setWindowTitle(QStringLiteral("会议时间轴 · 桌面挂载实验"));
  if (app.arguments().contains("--experiment-demo") ||
      app.arguments().contains("--experiment-check")) {
    auto timeline = window.findChild<Timeline *>();
    timeline->conferences.clear();
    for (int i = 0; i < 3; i++) {
      Conference c;
      c.name = QStringList{"VISION 2027", "LEARNING 2027", "SYSTEMS 2027"}[i];
      c.note = QStringLiteral("桌面挂载实验，未保存的演示记录");
      for (int j = 0; j < 5; j++)
        c.dates[j] = QDate::currentDate().addDays(-7 + i * 4 + j * 9);
      timeline->conferences.append(c);
    }
    timeline->refresh();
    if(auto status=window.findChild<QLabel*>("conferenceStatus"))status->setText(QStringLiteral("3 个演示会议"));
  }
  window.show();
  std::unique_ptr<DesktopAttachment> attachment;
  if (experiment) {
    attachment = std::make_unique<DesktopAttachment>(&window);
    QObject::connect(attachment.get(), &DesktopAttachment::recoveryRequired,
                     &window, &CalendarWindow::recreateDesktopWindow);
    QTimer::singleShot(250, attachment.get(), [&] { attachment->start(); });
  }
  auto snapshot = [&] {
    int i = app.arguments().indexOf("--snapshot");
    return i < 0 || i + 1 >= app.arguments().size() ||
           window.grab().save(app.arguments()[i + 1]);
  };
  if (app.arguments().contains("--experiment-check"))
    QTimer::singleShot(12000, &app, [&] { app.exit(5); });
  if (app.arguments().contains("--experiment-check"))
    QTimer::singleShot(1200, &app, [&] {
      QJsonObject report{
          {"attached", attachment && attachment->isAttached()},
          {"diagnostic",
           attachment ? attachment->diagnostic() : QString("disabled")},
          {"qtAlpha", window.grab().toImage().pixelColor(0, 0).alpha()}};
#ifdef Q_OS_WIN
      HWND handle = reinterpret_cast<HWND>(window.winId());
      auto parent = GetParent(handle);
      wchar_t cls[128] = {};
      GetClassNameW(parent, cls, 128);
      report["parentClass"] = QString::fromWCharArray(cls);
      report["childStyle"] =
          bool(GetWindowLongPtrW(handle, GWL_STYLE) & WS_CHILD);
      report["layered"] =
          bool(GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_LAYERED);
      RECT r = {};
      GetWindowRect(handle, &r);
      report["nativeBounds"] = QString("%1,%2,%3,%4")
                                   .arg(r.left)
                                   .arg(r.top)
                                   .arg(r.right)
                                   .arg(r.bottom);
#endif
      bool editor = false;
      QTimer::singleShot(0, &window, [&] {
        if (auto dialog =
                qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
          editor = true;
          dialog->reject();
        }
      });
      window.findChild<QPushButton *>("addButton")->click();
      report["editorOpened"] = editor;
      report["stillAttachedAfterEditor"] =
          attachment && attachment->isAttached() &&
          window.property("desktopAttached").toBool();
#ifdef Q_OS_WIN
      report["stillAttachedAfterEditor"] =
          report["stillAttachedAfterEditor"].toBool() &&
          GetParent(reinterpret_cast<HWND>(window.winId())) == parent;
#endif
      if (attachment) {
        attachment->detach();
        report["detached"] = !window.property("desktopAttached").toBool();
        if (report["attached"].toBool()) {
          window.recreateDesktopWindow();
          report["reattachedAfterWindowRecreation"] = attachment->attach();
        }
      }
      report["snapshotSaved"] = snapshot();
      if (attachment)
        attachment->detach();
      int i = app.arguments().indexOf("--report");
      if (i >= 0 && i + 1 < app.arguments().size()) {
        QSaveFile file(app.arguments()[i + 1]);
        if (file.open(QIODevice::WriteOnly)) {
          file.write(QJsonDocument(report).toJson());
          file.commit();
        }
      }
      app.exit(report["attached"].toBool() &&
                       report["stillAttachedAfterEditor"].toBool() &&
                       report["reattachedAfterWindowRecreation"].toBool() &&
                       editor && report["snapshotSaved"].toBool()
                   ? 0
                   : 3);
    });
  else if (app.arguments().contains("--smoke-test"))
    QTimer::singleShot(1000, &app, [&] { app.exit(snapshot() ? 0 : 2); });
  return app.exec();
}
