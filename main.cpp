#include "Calendar/CalendarWindow.h"
#include <QApplication>
#include <QScreen>
int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("PaperCalendar");
  QCoreApplication::setApplicationName("PaperCalendar");
  CalendarWindow window;
  window.show();
  if (app.arguments().contains("--smoke-test"))
    QTimer::singleShot(700, &app, [&] {
      const int i = app.arguments().indexOf("--snapshot");
      if (i >= 0 && i + 1 < app.arguments().size() &&
          !window.grab().save(app.arguments()[i + 1]))
        app.exit(2);
      else
        app.quit();
    });
  return app.exec();
}
