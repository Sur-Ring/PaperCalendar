#include "Calendar/CalendarWindow.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
class CalendarTests : public QObject {
  Q_OBJECT
  QString path() const {
    return QCoreApplication::arguments().at(2) + "/conferences.json";
  }
  QPushButton *button(CalendarWindow &w, const QString &text) {
    for (auto b : w.findChildren<QPushButton *>())
      if (b->text() == text)
        return b;
    return nullptr;
  }
private slots:
  void init() {
    QFile::remove(path());
    QFile::remove(QCoreApplication::arguments().at(2) + "/settings.ini");
  }
  void createValidateAndReload() {
    CalendarWindow window;
    window.show();
    QTimer::singleShot(0, [] {
      auto d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(d);
      auto name = d->findChild<QLineEdit *>();
      name->setText(QStringLiteral("  TEST 2027  "));
      QList<QLineEdit*> dates;for(int i=0;i<5;i++)dates.append(d->findChild<QLineEdit*>(QString("date%1").arg(i)));
      QCOMPARE(dates.size(), 5);
      QDate base = QDate::currentDate();
      for (int i = 0; i < 5; ++i)
        dates[i]->setText(base.addDays(i * 10).toString(Qt::ISODate));
      auto box = d->findChild<QDialogButtonBox *>();
      dates[1]->setText(base.addDays(-1).toString(Qt::ISODate));
      box->button(QDialogButtonBox::Save)->click();
      QVERIFY(
          d->isVisible()); // Invalid date ordering must keep the editor open.
      dates[1]->setText(base.addDays(10).toString(Qt::ISODate));
      box->button(QDialogButtonBox::Save)->click();
    });
    auto add = button(window, QStringLiteral("＋ 新建"));
    QVERIFY(add);
    add->click();
    QFile file(path());
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto json = QJsonDocument::fromJson(file.readAll());
    file.close();
    QCOMPARE(json.object()["version"].toInt(), 2);
    auto rows = json.object()["conferences"].toArray();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].toObject()["name"].toString(), QString("TEST 2027"));
    CalendarWindow restored;
    auto timeline = restored.findChild<Timeline *>();
    QVERIFY(timeline);
    QCOMPARE(timeline->conferences.size(), 1);
    QCOMPARE(timeline->conferences[0].dates[3],
             QDate::currentDate().addDays(30));
  }
  void corruptFileIsProtected() {
    QFile file(path());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{broken");
    file.close();
    CalendarWindow window;
    QVERIFY(!window.load());
    QTimer::singleShot(0, [] {
      if (auto box =
              qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
        box->accept();
    });
    QVERIFY(!window.save());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("{broken"));
  }
  void timelineSelectionAndZoom() {
    Timeline t;
    t.resize(1000, 400);
    Conference c;
    c.name = "Demo";
    for (int j = 0; j < 5; ++j)
      c.dates[j] = QDate::currentDate().addDays(j * 7);
    t.conferences.append(c);
    t.refresh();
    t.show();
    QSignalSpy picked(&t, &Timeline::picked);
    QTest::mouseClick(&t, Qt::LeftButton, Qt::NoModifier, QPoint(50, Timeline::AxisHeight+20));
    QCOMPARE(t.selected, 0);
    QCOMPARE(picked.count(), 1);
    QDate oldStart = t.start;
    QWheelEvent pan(QPointF(500, 120), QPointF(500, 120), QPoint(),
                    QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
    QApplication::sendEvent(&t, &pan);
    QCOMPARE(t.days, 90);
    QVERIFY(t.start > oldStart);
    QDate center = t.start.addDays(t.days / 2);
    QWheelEvent zoom(QPointF(500, 120), QPointF(500, 120), QPoint(),
                     QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                     Qt::NoScrollPhase, false);
    QApplication::sendEvent(&t, &zoom);
    QVERIFY(t.days < 90);
    QCOMPARE(t.start.addDays(t.days / 2), center);
  }
  void labelCollisions() {
    for (double width : {235., 430.})
      for (auto anchors :
           QVector<QVector<double>>{{0, 0, 0, 1},
                                    {width - 1, width - 1, width, width},
                                    {0, 1, width - 1, width},
                                    {100, 100, 100, 100}}) {
        QVector<QPair<int, double>> nodes;
        for (int i = 0; i < 4; i++)
          nodes.append({i, anchors[i]});
        auto labels = Timeline::arrangeLabels(nodes, width, 38);
        QCOMPARE(labels.size(), 4);
        for (int i = 0; i < labels.size(); i++) {
          QVERIFY(labels[i].rect.left() >= 0);
          QVERIFY(labels[i].rect.right() <= width);
          QVERIFY(labels[i].rect.bottom() < Timeline::RowHeight);
          for (int j = i + 1; j < labels.size(); j++)
            QVERIFY(!labels[i].rect.intersects(labels[j].rect));
        }
      }
  }
  void lockAndReorderPersist() {
    CalendarWindow w;
    auto t = w.findChild<Timeline *>();
    for (int i = 0; i < 3; i++) {
      Conference c;
      c.id = QString::number(i);
      c.name = QString("Conference%1").arg(i);
      for (auto &d : c.dates)
        d = QDate::currentDate();
      t->conferences.append(c);
    }
    QVERIFY(w.save());
    t->reorderRequested(0, 2);
    QCOMPARE(t->conferences[2].id, QString("0"));
    auto lock = w.findChild<QPushButton *>("lockButton");
    QVERIFY(lock);
    QVERIFY(lock->text().isEmpty());QVERIFY(!lock->icon().isNull());
    lock->click();
    QVERIFY(lock->isChecked());
    CalendarWindow restored;
    auto loaded = restored.findChild<Timeline *>();
    QCOMPARE(loaded->conferences[2].id, QString("0"));
    QVERIFY(restored.findChild<QPushButton *>("lockButton")->isChecked());
  }
  void colorAndNameEditing() {
    CalendarWindow window;
    window.show();
    auto t = window.findChild<Timeline *>();
    Conference c;
    c.name = "Editable";
    for (auto &date : c.dates)
      date = QDate::currentDate();
    t->conferences.append(c);
    t->refresh();
    QTimer::singleShot(0, [] {
      auto d = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
      QVERIFY(d);
      d->setCurrentColor(QColor("#e71955"));
      d->accept();
    });
    window.findChild<QPushButton *>("legend0")->click();
    QCOMPARE(t->colors[0], QColor("#e71955"));
    CalendarWindow restored;
    QCOMPARE(restored.findChild<Timeline *>()->colors[0], QColor("#e71955"));
    window.findChild<QPushButton *>("lockButton")->click();
    QTimer::singleShot(0, [] {
      auto d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(d);
      QCOMPARE(d->findChild<QLineEdit *>()->text(), QString("Editable"));
      d->reject();
    });
    QTest::mouseClick(t, Qt::LeftButton, Qt::NoModifier,
                      QPoint(50, Timeline::AxisHeight + 20));
  }
  void labelContrastAndNoPrepInterval() {
    auto luminance = [](QColor c) {
      auto f = [](double v) {
        v /= 255;
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
      };
      return .2126 * f(c.red()) + .7152 * f(c.green()) + .0722 * f(c.blue());
    };
    for (QColor color : QVector<QColor>{QColor("#538e88"), QColor("#6085b8"),
                                        QColor("#c69c55"), QColor("#a47796"),
                                        Qt::white, Qt::yellow, Qt::black})
      QVERIFY((luminance(Timeline::tint(color)) + .05) /
                  (luminance(Timeline::ink(color)) + .05) >=
              7);
    Timeline t;
    t.resize(560, 150);
    Conference c;
    c.name = "No prep";
    for (int j = 0; j < 5; j++)
      c.dates[j] = t.start.addDays(20 + j * 15);
    t.conferences.append(c);
    t.refresh();
    t.show();
    auto image = t.grab().toImage();
    QCOMPARE(
        image.pixelColor(
            qRound((Timeline::NameWidth + 10) * image.devicePixelRatio()),
            qRound((Timeline::AxisHeight + 27) * image.devicePixelRatio())),
        t.palette().color(QPalette::Window));
  }
  void fiveDenseLabels() {
    QVector<QPair<int, double>> nodes;
    for (int i = 0; i < 5; i++)
      nodes.append({i, 100});
    auto labels = Timeline::arrangeLabels(nodes, 235, 38);
    QCOMPARE(labels.size(), 5);
    for (int i = 0; i < 5; i++)
      for (int j = i + 1; j < 5; j++)
        QVERIFY(!labels[i].rect.intersects(labels[j].rect));
  }
  void legacyDataMigration() {
    QFile f(path());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(
        R"({"version":1,"conferences":[{"id":"old","name":"Old","dates":["2026-10-01","2026-10-02","2026-10-03","2026-10-04"]}]})");
    f.close();
    CalendarWindow w;
    auto t = w.findChild<Timeline *>();
    QCOMPARE(t->conferences.size(), 1);
    QVERIFY(!t->conferences[0].dates[4].isValid());
    QVERIFY(w.save());
    CalendarWindow restored;
    QVERIFY(
        !restored.findChild<Timeline *>()->conferences[0].dates[4].isValid());
    QVERIFY(w.findChildren<QComboBox *>().isEmpty());
    QVERIFY(w.testAttribute(Qt::WA_TranslucentBackground));
    auto image = w.grab().toImage();
    QCOMPARE(image.pixelColor(0, 0).alpha(), 128);
  }
  void blankDatesAllowed(){CalendarWindow window;QTimer::singleShot(0,[]{auto d=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(d);QVERIFY(d->findChildren<QCheckBox*>().isEmpty());d->findChild<QLineEdit*>()->setText("Unknown dates");for(int i=0;i<5;i++)QVERIFY(d->findChild<QLineEdit*>(QString("date%1").arg(i))->text().isEmpty());d->findChild<QLineEdit*>("date1")->setText("2026/11/15");d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();});window.findChild<QPushButton*>("addButton")->click();CalendarWindow restored;auto t=restored.findChild<Timeline*>();QCOMPARE(t->conferences.size(),1);QVERIFY(!t->conferences[0].dates[0].isValid());QCOMPARE(t->conferences[0].dates[1],QDate(2026,11,15));QVERIFY(!t->conferences[0].dates[4].isValid());}
  void startupUsesTodayPosition(){
    {QSettings settings(QCoreApplication::arguments().at(2)+"/settings.ini",QSettings::IniFormat);settings.setValue("days",180);settings.setValue("start","2020-01-01");settings.sync();}
    CalendarWindow w;auto timeline=w.findChild<Timeline*>();QCOMPARE(timeline->days,180);QDate expected=QDate::currentDate().addDays(-45);QCOMPARE(timeline->start,expected);
    QVERIFY(!w.findChild<QPushButton*>("todayButton"));timeline->start=expected.addYears(-2);timeline->grab();timeline->findChild<QPushButton*>("todayOverflow")->click();QCOMPARE(timeline->start,expected);
  }
  void todayOutsideVisibleRange(){
    Timeline t;t.resize(560,150);t.show();auto button=t.findChild<QPushButton*>("todayOverflow");QVERIFY(button);
    t.start=QDate::currentDate().addDays(1);t.grab();QVERIFY(button->isVisible());QVERIFY(button->text().startsWith(QStringLiteral("←")));QVERIFY(button->text().contains(QDate::currentDate().toString("MM/dd")));QVERIFY(!button->text().contains(QStringLiteral("范围外")));QVERIFY(button->x()>=Timeline::NameWidth);button->click();t.grab();QVERIFY(!button->isVisible());
    t.start=QDate::currentDate().addDays(-t.days-1);t.grab();QVERIFY(button->isVisible());QVERIFY(button->text().endsWith(QStringLiteral("→")));QVERIFY(button->geometry().right()<t.width());
    t.start=QDate::currentDate().addDays(-t.days);t.grab();QVERIFY(!button->isVisible());t.start=QDate::currentDate();t.grab();QVERIFY(!button->isVisible());
  }
  void nativeLockHitTest() {
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != "windows")
      QSKIP("Requires the native Windows platform");
    CalendarWindow window;
    window.show();
    QTest::qWait(30);
    auto h = reinterpret_cast<HWND>(window.winId());
    RECT r;
    GetWindowRect(h, &r);
    auto point = MAKELPARAM(r.left + 1, (r.top + r.bottom) / 2);
    QCOMPARE(SendMessageW(h, WM_NCHITTEST, 0, point), LRESULT(HTLEFT));
    window.findChild<QPushButton *>("lockButton")->click();
    QCOMPARE(SendMessageW(h, WM_NCHITTEST, 0, point), LRESULT(HTCLIENT));
#else
    QSKIP("Windows only");
#endif
  }
  void nodeCountdownTooltip(){
    QDate today(2028,2,28);QCOMPARE(Timeline::relativeDays(today.addDays(3),today),QStringLiteral("还有 3 天"));QCOMPARE(Timeline::relativeDays(today,today),QStringLiteral("今天"));QCOMPARE(Timeline::relativeDays(today.addDays(-2),today),QStringLiteral("已过 2 天"));QCOMPARE(Timeline::relativeDays(QDate(2028,3,1),today),QStringLiteral("还有 2 天"));QVERIFY(Timeline::relativeDays(QDate(),today).isEmpty());
    Timeline t;t.resize(560,120);Conference c;c.name="Hover";c.dates[0]=QDate::currentDate().addDays(5);t.conferences.append(c);t.refresh();t.show();int x=Timeline::NameWidth+qRound(t.start.daysTo(c.dates[0])*double(t.width()-Timeline::NameWidth)/t.days);QPoint point(x,Timeline::AxisHeight+3);QHelpEvent help(QEvent::ToolTip,point,t.mapToGlobal(point));QApplication::sendEvent(&t,&help);QVERIFY(QToolTip::text().contains(QStringLiteral("还有 5 天")));QVERIFY(QToolTip::text().contains(c.dates[0].toString(Qt::ISODate)));QToolTip::hideText();
  }
  void clickExpandsNode(){
    Timeline t;t.resize(560,120);Conference c;c.name="Click details";c.dates[0]=QDate::currentDate().addDays(5);t.conferences.append(c);t.refresh();t.show();int x=Timeline::NameWidth+qRound(t.start.daysTo(c.dates[0])*double(t.width()-Timeline::NameWidth)/t.days);QTest::mouseClick(&t,Qt::LeftButton,Qt::NoModifier,QPoint(x,Timeline::AxisHeight+3));auto menu=t.findChild<QMenu*>("nodeDetails");QVERIFY(menu);QVERIFY(menu->isVisible());QVERIFY(menu->findChild<QLabel*>("nodeDetailsText")->text().contains(QStringLiteral("还有 5 天")));QTest::keyClick(menu,Qt::Key_Escape);QVERIFY(!menu->isVisible());
  }
  void snapshot() {
    CalendarWindow window;
    auto t = window.findChild<Timeline *>();
    for (int i = 0; i < 3; ++i) {
      Conference c;
      c.name = QStringList{"VISION 2027", "LEARNING 2027", "SYSTEMS 2027"}[i];
      c.note = QStringLiteral("演示数据 / 论文投稿计划");
      for (int j = 0; j < 5; ++j)
        c.dates[j] = QDate::currentDate().addDays(-12 + i * 12 + j * 16);
      t->conferences.append(c);
    }
    t->refresh();
    QVERIFY(window.save());
    window.show();
    QTest::qWait(100);
    QVERIFY(window.grab().save(QDir::currentPath() + "/qt-preview.png"));
  }
};
int main(int argc, char **argv) {
  QTemporaryDir directory(QDir::currentPath() + "/calendar-tests-XXXXXX");
  if (!directory.isValid())
    return 2;
  QByteArray program = argv[0], flag = "--data-dir",
             path = directory.path().toUtf8();
  char *args[] = {program.data(), flag.data(), path.data(), nullptr};
  int count = 3;
  QApplication app(count, args);
#ifdef Q_OS_WIN
  QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
  QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
#endif
  QCoreApplication::setOrganizationName("PaperCalendarTests");
  QCoreApplication::setApplicationName("PaperCalendarTests");
  CalendarTests tests;
  return QTest::qExec(
      &tests, QStringList{QStringLiteral("CalendarTests"), QStringLiteral("-o"),
                          QDir::currentPath() +
                              QStringLiteral("/test-results.txt,txt")});
}
#include "calendar_tests.moc"
