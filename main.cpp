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
namespace {
QString experimentLogPath;
void experimentLog(QtMsgType type,const QMessageLogContext &,const QString &message){
 QFile file(experimentLogPath);if(file.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text)){QTextStream stream(&file);stream<<QDateTime::currentDateTime().toString(Qt::ISODateWithMs)<<" ["<<int(type)<<"] "<<message<<"\n";}
 if(type==QtFatalMsg)abort();
}
}
int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("PaperCalendar");
  // Automatic desktop attachment is disabled after actual visibility failed
  // on this machine. Retain only an explicit opt-in for future diagnostics.
  bool experiment = app.arguments().contains("--desktop-host");
  QCoreApplication::setApplicationName(experiment ? "PaperCalendarWorkerW"
                                                  : "PaperCalendar");
  if (experiment){
    app.setQuitOnLastWindowClosed(false);
    auto args=app.arguments();int index=args.indexOf("--data-dir");QString directory=index>=0&&index+1<args.size()?QDir(args[index+1]).absolutePath():QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);QDir().mkpath(directory);experimentLogPath=directory+"/desktop-experiment.log";qInstallMessageHandler(experimentLog);qInfo()<<"Experimental process"<<QCoreApplication::applicationPid();
  }
  std::unique_ptr<QLockFile> experimentLock;
  if(experiment&&!app.arguments().contains("--experiment-check")){
    experimentLock=std::make_unique<QLockFile>(QStandardPaths::writableLocation(QStandardPaths::TempLocation)+"/PaperCalendarWorkerW-experiment.lock");experimentLock->setStaleLockTime(0);
    if(!experimentLock->tryLock(0)){QMessageBox::information(nullptr,QStringLiteral("实验程序已在运行"),QStringLiteral("请先检查桌面或托盘中的实验程序，无需重复打开。"));return 0;}
  }
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
  QMenu experimentMenu;
  std::unique_ptr<QSystemTrayIcon> experimentTray;
  if(experiment&&QSystemTrayIcon::isSystemTrayAvailable()){
    experimentTray=std::make_unique<QSystemTrayIcon>(app.style()->standardIcon(QStyle::SP_ComputerIcon));experimentTray->setToolTip(QStringLiteral("PaperCalendar 桌面实验（双击恢复窗口）"));
    auto restore=[&]{if(attachment)attachment->detach();window.show();window.raise();window.activateWindow();};
    experimentMenu.addAction(QStringLiteral("恢复为普通窗口"),&window,restore);
    experimentMenu.addAction(QStringLiteral("重新挂到桌面"),&window,[&]{if(attachment)attachment->start();});
    experimentMenu.addSeparator();experimentMenu.addAction(QStringLiteral("退出实验"),&window,[&]{window.close();});
    experimentTray->setContextMenu(&experimentMenu);QObject::connect(experimentTray.get(),&QSystemTrayIcon::activated,&window,[&,restore](QSystemTrayIcon::ActivationReason reason){if(reason==QSystemTrayIcon::DoubleClick)restore();});experimentTray->show();
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
