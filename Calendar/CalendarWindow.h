#pragma once
#include <QtWidgets>
struct Conference {
  QString id, name, note;
  QDate dates[5];
  QJsonObject json() const;
  static Conference fromJson(const QJsonObject &);
};
struct DateLabel {
  int stage;
  double anchor;
  QRectF rect;
};
class Timeline : public QWidget {
  Q_OBJECT
public:
  explicit Timeline(QWidget *parent = nullptr);
  QVector<Conference> conferences;
  QVector<QColor> colors{QColor("#538e88"), QColor("#6085b8"),
                         QColor("#c69c55"), QColor("#a47796"),
                         QColor("#72925b")};
  QDate start;
  int days = 90, selected = -1;
  static constexpr int NameWidth = 124, AxisHeight = 32, RowHeight = 40;
  static QVector<DateLabel>
  arrangeLabels(const QVector<QPair<int, double>> &nodes, double width,
                double labelWidth);
  static QString relativeDays(QDate date, QDate today=QDate::currentDate());
  static QColor tint(QColor color);
  static QColor ink(QColor color);
  void refresh();
  void setScrollOffset(int value);
  int scrollOffset() const {return headerOffset;}
signals:
  void rangeChanged();
  void scrollRequested(int delta);
  void picked(int index);
  void editRequested(int index);
  void reorderRequested(int from, int to);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void dragEnterEvent(QDragEnterEvent *) override;
  void dragMoveEvent(QDragMoveEvent *) override;
  void dragLeaveEvent(QDragLeaveEvent *) override;
  void dropEvent(QDropEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  bool event(QEvent *) override;

private:
  int headerOffset=0;
  QPushButton *todayOverflow;
  QPoint press;
  int pressedRow = -1, insertion = -1;
  int rowAt(int y) const;
  int nodeAt(QPoint point) const;
  void expandNode(int row,int stage,QPoint globalPosition);
  bool validDrag(const QMimeData *) const;
};
class CalendarWindow : public QWidget {
  Q_OBJECT
public:
  CalendarWindow();
  bool load();
  bool save();
  void recreateDesktopWindow();

protected:
  void closeEvent(QCloseEvent *) override;
  void paintEvent(QPaintEvent *) override;
  bool eventFilter(QObject *, QEvent *) override;
#ifdef Q_OS_WIN
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  bool nativeEvent(const QByteArray &, void *, qintptr *) override;
#else
  bool nativeEvent(const QByteArray &, void *, long *) override;
#endif
#endif
private:
  Timeline *timeline;
  QLabel *status;
  QPushButton *lockButton;
  QSettings settings;
  QString dataPath;
  bool writable = true, locked = false;
  void edit(int index);
  void reorder(int from, int to);
  void setLocked(bool value);
};
