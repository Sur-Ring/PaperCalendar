#include "CalendarWindow.h"
#include "DesktopController.h"
#include <QSaveFile>
#include <QUuid>
#include <cmath>
#include <limits>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <windowsx.h>
#endif
namespace {
QString storageDirectory() {
  auto args = QCoreApplication::arguments();
  int i = args.indexOf("--data-dir");
  if (i >= 0 && i + 1 < args.size())
    return QDir(args[i + 1]).absolutePath();
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
const QStringList stages = {
    QStringLiteral("摘要截止"), QStringLiteral("正文截止"),
    QStringLiteral("审稿发布"), QStringLiteral("回复截止"),
    QStringLiteral("最终结果")};
const QStringList intervals = {
    QString(), QStringLiteral("摘要截止至正文截止"), QStringLiteral("等待审稿"),
    QStringLiteral("意见回复"), QStringLiteral("等待结果")};
QIcon lockIcon(bool locked){
 QIcon icon;
 for(int size:{24,48,72}){QPixmap pixmap(size,size);pixmap.fill(Qt::transparent);QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.scale(size/24.0,size/24.0);p.setPen(QPen(QColor("#eef2ef"),1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);p.drawRoundedRect(QRectF(5,10,14,11),2,2);QPainterPath shackle;double left=locked?8:10,right=locked?16:18;shackle.moveTo(left,10);shackle.lineTo(left,6);shackle.cubicTo(left,1,right,1,right,6);if(locked)shackle.lineTo(right,10);p.drawPath(shackle);p.drawEllipse(QPointF(12,15),.8,.8);p.drawLine(QPointF(12,16),QPointF(12,18));p.end();icon.addPixmap(pixmap);}
 return icon;
}
constexpr auto RowMime = "application/x-paper-calendar-row";
int dropY(const QDropEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return qRound(event->position().y());
#else
  return event->pos().y();
#endif
}
QFont smallFont(bool bold = false) {
  QFont f("Microsoft YaHei UI");
  f.setPixelSize(10);
  f.setBold(bold);
  return f;
}
} // namespace
QJsonObject Conference::json() const {
  QJsonArray a;
  for (auto d : dates)
    a.append(d.isValid() ? QJsonValue(d.toString(Qt::ISODate)) : QJsonValue());
  return {{"id", id}, {"name", name}, {"note", note}, {"dates", a}};
}
Conference Conference::fromJson(const QJsonObject &o) {
  Conference c;
  c.id = o["id"].toString();
  c.name = o["name"].toString();
  c.note = o["note"].toString();
  auto a = o["dates"].toArray();
  for (int i = 0; i < 5; i++)
    c.dates[i] = i < a.size()
                     ? QDate::fromString(a.at(i).toString(), Qt::ISODate)
                     : QDate();
  return c;
}
QString Timeline::relativeDays(QDate date,QDate today){
 if(!date.isValid()||!today.isValid())return QString();
 qint64 days=today.daysTo(date);
 return days>0?QStringLiteral("还有 %1 天").arg(days):days<0?QStringLiteral("已过 %1 天").arg(-days):QStringLiteral("今天");
}
QColor Timeline::tint(QColor c) {
  return QColor(qRound(c.red() * .15 + 255 * .85),
                qRound(c.green() * .15 + 255 * .85),
                qRound(c.blue() * .15 + 255 * .85));
}
QColor Timeline::ink(QColor c) {
  auto lum = [](QColor v) {
    auto f = [](double x) {
      x /= 255;
      return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
    };
    return .2126 * f(v.red()) + .7152 * f(v.green()) + .0722 * f(v.blue());
  };
  const double bg = lum(tint(c));
  while ((bg + .05) / (lum(c) + .05) < 7)
    c = QColor(int(c.red() * .9), int(c.green() * .9), int(c.blue() * .9));
  return c;
}
QVector<DateLabel>
Timeline::arrangeLabels(const QVector<QPair<int, double>> &nodes, double width,
                        double labelWidth) {
  QVector<DateLabel> best;
  double bestScore = std::numeric_limits<double>::max();
  for (int mask = 0; mask < (1 << nodes.size()); mask++) {
    QVector<DateLabel> placement;
    double score = 0;
    bool valid = true;
    for (int lane = 0; lane < 2; lane++) {
      QVector<QPair<int, double>> group;
      for (int i = 0; i < nodes.size(); i++)
        if (((mask >> i) & 1) == lane)
          group.append(nodes[i]);
      if (group.isEmpty())
        continue;
      if (group.size() * labelWidth + (group.size() - 1) * 4 > width - 6) {
        valid = false;
        break;
      }
      QVector<double> positions;
      for (int i = 0; i < group.size(); i++)
        positions.append(qMax(group[i].second - labelWidth / 2,
                              i ? positions[i - 1] + labelWidth + 4 : 3.0));
      positions.last() = qMin(positions.last(), width - 3 - labelWidth);
      for (int i = positions.size() - 2; i >= 0; i--)
        positions[i] = qMin(positions[i], positions[i + 1] - labelWidth - 4);
      double shift = 0;
      for (int i = 0; i < group.size(); i++)
        shift += group[i].second - labelWidth / 2 - positions[i];
      shift /= group.size();
      shift = qBound(3 - positions[0], shift,
                     width - 3 - positions.last() - labelWidth);
      for (int i = 0; i < group.size(); i++) {
        double left = positions[i] + shift;
        score +=
            std::pow(left + labelWidth / 2 - group[i].second, 2) + lane * .25;
        placement.append({group[i].first, group[i].second,
                          QRectF(left, lane ? 7 : 23, labelWidth, 15)});
      }
    }
    if (valid && score < bestScore) {
      bestScore = score;
      best = placement;
    }
  }
  return best;
}
Timeline::Timeline(QWidget *p)
    : QWidget(p), start(QDate::currentDate().addDays(-22)) {
  todayOverflow=new QPushButton(this);todayOverflow->setObjectName("todayOverflow");
  todayOverflow->setStyleSheet("QPushButton{font-size:10px;color:#ffa58e;background:rgba(48,48,48,210);border:0;padding:0 3px;border-radius:2px} QPushButton:hover{background:rgba(90,60,50,230)}");
  todayOverflow->setToolTip(QStringLiteral("点击回到今天"));todayOverflow->hide();
  connect(todayOverflow,&QPushButton::clicked,this,[this]{start=QDate::currentDate().addDays(-days/4);update();});
  setMinimumWidth(360);
  setMouseTracking(true);
  setAcceptDrops(true);
  setFocusPolicy(Qt::StrongFocus);
  refresh();
}
void Timeline::refresh() {
  setMinimumHeight(AxisHeight + qMax(1, conferences.size()) * RowHeight);
  update();
}
int Timeline::rowAt(int y) const {
  return y < AxisHeight ? -1
         : (y - AxisHeight) / RowHeight < conferences.size()
             ? (y - AxisHeight) / RowHeight
             : -1;
}
void Timeline::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const double tw = width() - NameWidth, scale = tw / days;
  auto x = [&](QDate d) { return NameWidth + start.daysTo(d) * scale; };
  double today = x(QDate::currentDate());
  int tick = days <= 45 ? 7 : days <= 120 ? 14 : days <= 240 ? 30 : 60;
  while (tick * scale < 48)
    tick *= 2;
  p.setFont(smallFont());
  p.setPen(QColor("#d3d9d5"));
  p.drawText(QRect(10, 9, 100, 20), Qt::AlignVCenter, QStringLiteral("会议"));
  for (int n = 0; n < days; n += tick) {
    double px = x(start.addDays(n));
    p.setPen(QColor(255, 255, 255, 35));
    p.drawLine(QPointF(px, AxisHeight), QPointF(px, height()));
    p.setPen(QColor("#d3d9d5"));
    p.drawText(QRectF(qBound(double(NameWidth), px - 21, double(width() - 43)),
                      14, 42, 17),
               Qt::AlignCenter, start.addDays(n).toString("MM/dd"));
  }
  for (int i = 0; i < conferences.size(); i++) {
    const auto &c = conferences[i];
    int y = AxisHeight + i * RowHeight;
    p.setPen(QColor(255, 255, 255, 35));
    p.drawLine(0, y, width(), y);
    p.drawLine(NameWidth, y, NameWidth, y + RowHeight);
    p.setFont(smallFont());
    p.setPen(QColor("#9aa59b"));
    p.setBrush(QColor("#9aa59b"));
    p.setPen(Qt::NoPen);
    for (int dy : {-5, 0, 5})
      for (int dx : {7, 12})
        p.drawEllipse(QPointF(dx, y + RowHeight / 2 + dy), 1, 1);
    QFont nameFont("Microsoft YaHei UI");
    nameFont.setPixelSize(12);
    nameFont.setBold(true);
    p.setFont(nameFont);
    p.setPen(QColor("#eef2ef"));
    p.drawText(
        QRect(23, y, NameWidth - 27, RowHeight), Qt::AlignVCenter,
        p.fontMetrics().elidedText(c.name, Qt::ElideRight, NameWidth - 27));
    p.save();
    p.setClipRect(QRect(NameWidth, y, int(tw), RowHeight));
    for (int j = 1; j < 5; j++) {
      if (!c.dates[j].isValid() || !c.dates[j-1].isValid())
        continue;
      QColor background = colors[j];
      background.setAlpha(30);
      p.fillRect(QRectF(x(c.dates[j - 1]), y,
                        qMax(0.0, x(c.dates[j]) - x(c.dates[j - 1])),
                        RowHeight),
                 background);
    }
    if (today >= NameWidth && today <= width()) {
      p.setPen(QPen(QColor("#d66147"), 1.5));
      p.drawLine(QPointF(today, y), QPointF(today, y + RowHeight));
    }
    QVector<QPair<int, double>> nodes;
    for (int j = 0; j < 5; j++) {
      if (!c.dates[j].isValid())
        continue;
      double px = x(c.dates[j]);
      if (px < NameWidth || px > width())
        continue;
      nodes.append({j, px - NameWidth});
      p.setPen(QPen(colors[j], 1));
      p.drawLine(QPointF(px, y), QPointF(px, y + RowHeight));
      p.setBrush(colors[j]);
      p.setPen(QColor("#faf9f5"));
      p.drawEllipse(QPointF(px, y + 3), 2.5, 2.5);
    }
    p.setFont(smallFont(true));
    double labelWidth = QFontMetricsF(p.font()).horizontalAdvance("00/00") + 8;
    auto placed = arrangeLabels(nodes, tw, labelWidth);
    for (const auto &label : placed) {
      QRectF r = label.rect.translated(NameWidth, y);
      p.setPen(QPen(colors[label.stage], 1));
      p.drawLine(QPointF(NameWidth + label.anchor, y + 3),
                 QPointF(r.center().x(), r.top()));
    }
    for (const auto &label : placed) {
      QRectF r = label.rect.translated(NameWidth, y);
      p.setPen(QPen(colors[label.stage], .8));
      p.setBrush(tint(colors[label.stage]));
      p.drawRoundedRect(r, 2, 2);
      p.setPen(ink(colors[label.stage]));
      p.drawText(r, Qt::AlignCenter, c.dates[label.stage].toString("MM/dd"));
    }
    p.restore();
  }
  bool outside=today<NameWidth||today>width();
  todayOverflow->setVisible(outside);
  if(outside){
    bool left=today<NameWidth;todayOverflow->setText(left?QStringLiteral("← 今天 %1").arg(QDate::currentDate().toString("MM/dd")):QStringLiteral("今天 %1 →").arg(QDate::currentDate().toString("MM/dd")));
    todayOverflow->adjustSize();todayOverflow->resize(todayOverflow->sizeHint().width(),15);
    todayOverflow->move(left?NameWidth+3:width()-todayOverflow->width()-3,0);todayOverflow->raise();
  }
  if (today >= NameWidth && today <= width()) {
    p.setFont(smallFont());
    p.setPen(QColor("#d66147"));
    p.drawLine(QPointF(today, 0), QPointF(today, AxisHeight));
    p.drawText(
        QRectF(qBound(double(NameWidth), today + 4, double(width() - 68)), 0,
               66, 18),
        Qt::AlignVCenter,
        QStringLiteral("今天 ") + QDate::currentDate().toString("MM/dd"));
  }
  if (insertion >= 0) {
    p.setPen(QPen(QColor("#eef2ef"), 2));
    int y = AxisHeight + insertion * RowHeight;
    p.drawLine(0, y, width(), y);
  }
  if (conferences.isEmpty()) {
    p.setPen(QColor("#d3d9d5"));
    p.drawText(QRect(0, AxisHeight, width(), RowHeight), Qt::AlignCenter,
               QStringLiteral("点击「＋ 新建」添加会议"));
  }
}
int Timeline::nodeAt(QPoint point) const {
 int row=rowAt(point.y());if(row<0||point.x()<NameWidth)return -1;
 const auto &c=conferences[row];double tw=width()-NameWidth;QVector<QPair<int,double>> nodes;
 for(int j=0;j<5;j++){if(!c.dates[j].isValid())continue;double x=start.daysTo(c.dates[j])*tw/days;if(x>=0&&x<=tw)nodes.append({j,x});}
 double lw=QFontMetricsF(smallFont(true)).horizontalAdvance("00/00")+8;
 for(const auto &l:arrangeLabels(nodes,tw,lw))if(l.rect.translated(NameWidth,AxisHeight+row*RowHeight).contains(point))return l.stage;
 for(auto node:nodes)if(std::abs(node.second+NameWidth-point.x())<5)return node.first;
 return -1;
}
void Timeline::expandNode(int row,int stage,QPoint globalPosition){
 auto menu=new QMenu(window()->property("desktopAttached").toBool()?nullptr:this);menu->setObjectName("nodeDetails");menu->setAttribute(Qt::WA_DeleteOnClose);menu->setStyleSheet("QMenu{background:#faf9f5;color:#293f3c;border:1px solid #a6b5a7;border-radius:4px} QLabel{background:#faf9f5;color:#293f3c;padding:10px;font-size:12px}");
 auto action=new QWidgetAction(menu);auto label=new QLabel(menu);label->setObjectName("nodeDetailsText");label->setTextFormat(Qt::PlainText);const auto &c=conferences[row];label->setText(c.name+"\n"+stages[stage]+"  "+c.dates[stage].toString(Qt::ISODate)+"\n"+relativeDays(c.dates[stage]));action->setDefaultWidget(label);menu->addAction(action);menu->popup(globalPosition);
}
void Timeline::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton)
    return;
  press = e->pos();
  pressedRow = rowAt(press.y());
  selected = pressedRow;
  if (selected >= 0)
    emit picked(selected);
  setFocus();
}
void Timeline::mouseReleaseEvent(QMouseEvent *e) {
  int row = rowAt(e->pos().y());
  if (e->button() == Qt::LeftButton && pressedRow >= 0 && row == pressedRow &&
      press.x() >= 20 && e->pos().x() >= 20 && e->pos().x() < NameWidth &&
      (e->pos() - press).manhattanLength() < QApplication::startDragDistance())
    emit editRequested(row);
  if(e->button()==Qt::LeftButton&&pressedRow>=0&&row==pressedRow&&press.x()>=NameWidth&&(e->pos()-press).manhattanLength()<QApplication::startDragDistance()){
    int stage=nodeAt(e->pos());if(stage>=0)expandNode(row,stage,mapToGlobal(e->pos()+QPoint(0,8)));
  }
  pressedRow = -1;
}
void Timeline::mouseMoveEvent(QMouseEvent *e) {
  setCursor(nodeAt(e->pos())>=0?Qt::PointingHandCursor:e->pos().x() < 20          ? Qt::OpenHandCursor
            : e->pos().x() < NameWidth ? Qt::PointingHandCursor
                                       : Qt::ArrowCursor);
  if ((e->buttons() & Qt::LeftButton) && pressedRow >= 0 && press.x() < 20 &&
      (e->pos() - press).manhattanLength() >=
          QApplication::startDragDistance()) {
    auto drag = new QDrag(this);
    auto mime = new QMimeData;
    mime->setData(RowMime, QByteArray::number(pressedRow));
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
    drag->deleteLater();
    pressedRow = -1;
    insertion = -1;
    update();
  }
}
void Timeline::wheelEvent(QWheelEvent *e) {
  int delta = e->angleDelta().y() ? e->angleDelta().y() : e->pixelDelta().y();
  if (!delta) {
    e->ignore();
    return;
  }
  if (e->modifiers() & Qt::ShiftModifier)
    emit scrollRequested(delta);
  else if (e->modifiers() & Qt::ControlModifier) {
    QDate center = start.addDays(days / 2);
    days = qBound(14, qRound(days * (delta > 0 ? .8 : 1.25)), 730);
    start = center.addDays(-days / 2);
    emit rangeChanged();
  } else
    start = start.addDays((delta > 0 ? -1 : 1) * qMax(1, qRound(days / 20.0)));
  update();
  e->accept();
}
bool Timeline::validDrag(const QMimeData *mime) const {
  bool ok = false;
  int from = mime->data(RowMime).toInt(&ok);
  return mime->hasFormat(RowMime) && ok && from >= 0 &&
         from < conferences.size();
}
void Timeline::dragEnterEvent(QDragEnterEvent *e) {
  if (e->source() == this && validDrag(e->mimeData()))
    e->acceptProposedAction();
}
void Timeline::dragMoveEvent(QDragMoveEvent *e) {
  if (e->source() != this || !validDrag(e->mimeData()))
    return;
  insertion = qBound(0, qRound((dropY(e) - AxisHeight) / double(RowHeight)),
                     int(conferences.size()));
  update();
  e->acceptProposedAction();
}
void Timeline::dragLeaveEvent(QDragLeaveEvent *e) {
  insertion = -1;
  update();
  e->accept();
}
void Timeline::dropEvent(QDropEvent *e) {
  if (e->source() != this || !validDrag(e->mimeData()))
    return;
  int from = e->mimeData()->data(RowMime).toInt();
  int dest = qBound(0, qRound((dropY(e) - AxisHeight) / double(RowHeight)),
                    int(conferences.size()));
  if (dest > from)
    --dest;
  insertion = -1;
  if (dest != from)
    emit reorderRequested(from, dest);
  update();
  e->acceptProposedAction();
}
void Timeline::keyPressEvent(QKeyEvent *e) {
  if ((e->modifiers() & Qt::AltModifier) && selected >= 0 &&
      (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
    int dest = selected + (e->key() == Qt::Key_Up ? -1 : 1);
    if (dest >= 0 && dest < conferences.size())
      emit reorderRequested(selected, dest);
    e->accept();
    return;
  }
  QWidget::keyPressEvent(e);
}
bool Timeline::event(QEvent *event) {
  if (event->type() == QEvent::ToolTip) {
    auto e = static_cast<QHelpEvent *>(event);
    int row = rowAt(e->pos().y());
    if (row >= 0) {
      const auto &c = conferences[row];
      QString text = c.name;
      if (e->pos().x() >= NameWidth) {
        double tw = width() - NameWidth;
        QVector<QPair<int, double>> nodes;
        for (int j = 0; j < 5; j++) {
          if (!c.dates[j].isValid())
            continue;
          double x = start.daysTo(c.dates[j]) * tw / days;
          if (x >= 0 && x <= tw)
            nodes.append({j, x});
        }
        double lw =
            QFontMetricsF(smallFont(true)).horizontalAdvance("00/00") + 8;
        auto placements = arrangeLabels(nodes, tw, lw);
        int stage = -1;
        for (const auto &l : placements)
          if (l.rect.translated(NameWidth, AxisHeight + row * RowHeight)
                  .contains(e->pos()))
            stage = l.stage;
        if (stage < 0)
          for (auto node : nodes)
            if (std::abs(node.second + NameWidth - e->pos().x()) < 5)
              stage = node.first;
        if (stage >= 0)
          text = stages[stage] + " " + c.dates[stage].toString(Qt::ISODate)+"\n"+relativeDays(c.dates[stage]);
        else {
          QDate d =
              start.addDays(qFloor((e->pos().x() - NameWidth) * days / tw));
          for (int j = 1; j < 5; j++)
            if (c.dates[j-1].isValid() && c.dates[j].isValid() && d >= c.dates[j - 1] && d < c.dates[j])
              text = intervals[j] + "\n" +
                     c.dates[j - 1].toString(Qt::ISODate) + " — " +
                     c.dates[j].toString(Qt::ISODate);
        }
      }
      QToolTip::showText(e->globalPos(), text, this);
      return true;
    }
  }
  return QWidget::event(event);
}
CalendarWindow::CalendarWindow()
    : settings(storageDirectory() + "/settings.ini", QSettings::IniFormat) {
  setWindowTitle(QStringLiteral("会议时间轴"));
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_QuitOnClose, true);
  setAttribute(Qt::WA_TranslucentBackground);
  resize(560, 240);
  setMinimumSize(450, 180);
  setStyleSheet(
      "QWidget{background:transparent;color:#eef2ef;font-family:'Microsoft "
      "YaHei "
      "UI';font-size:12px} QPushButton,QComboBox{padding:4px 7px;border:1px "
      "solid rgba(255,255,255,68);border-radius:4px;background:rgba(255,255,255,28)} "
      "QPushButton:hover{background:rgba(255,255,255,55)} "
      "QPushButton:checked{background:rgba(140,180,140,70);border-color:#"
      "a9c3a1} "
      "QTextEdit,QLineEdit,QDateEdit{background:white;border:1px solid "
      "#cdd5c9;padding:6px} QScrollArea{border:1px solid #dfe3da}");
  auto root = new QVBoxLayout(this);
  root->setContentsMargins(10, 8, 10, 7);
  root->setSpacing(5);
  auto header = new QWidget;
  header->setObjectName("dragHeader");
  header->installEventFilter(this);
  auto h = new QHBoxLayout(header);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(5);
  auto title = new QLabel(QStringLiteral("会议时间轴"));
  title->setStyleSheet("font-size:14px;font-weight:600");
  title->installEventFilter(this);
  title->setObjectName("dragHeader");
  h->addWidget(title, 1);
  lockButton = new QPushButton;
  lockButton->setFixedSize(30,26);lockButton->setIconSize(QSize(18,18));
  lockButton->setObjectName("lockButton");
  lockButton->setCheckable(true);
  h->addWidget(lockButton);
  auto add = new QPushButton(QStringLiteral("＋ 新建"));
  add->setObjectName("addButton");
  h->addWidget(add);
  auto close = new QPushButton("×");
  close->setToolTip(QStringLiteral("退出"));
  close->setFixedWidth(24);
  h->addWidget(close);
  root->addWidget(header);
  timeline = new Timeline;
  for (int i = 0; i < 5; i++) {
    QColor saved(
        settings.value(QString("color%1").arg(i), timeline->colors[i].name())
            .toString());
    if (saved.isValid())
      timeline->colors[i] = saved;
  }
  auto legend = new QHBoxLayout;
  legend->setSpacing(12);
  for (int i = 0; i < 5; i++) {
    auto b = new QPushButton(stages[i]);
    b->setObjectName(QString("legend%1").arg(i));
    auto style = [this, b, i] {
      QPixmap dot(8, 8);
      dot.fill(Qt::transparent);
      QPainter painter(&dot);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setPen(Qt::NoPen);
      painter.setBrush(timeline->colors[i]);
      painter.drawEllipse(QRectF(1, 1, 6, 6));
      b->setIcon(QIcon(dot));
      b->setIconSize(QSize(8, 8));
      b->setStyleSheet(
          "QPushButton{border:0;padding:0;background:transparent;color:#d3d9d5;font-size:10px}");
    };
    style();
    b->setToolTip(QStringLiteral("点击修改节点颜色"));
    legend->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, b, i, style] {
      QColor color = QColorDialog::getColor(
          timeline->colors[i], property("desktopAttached").toBool()?nullptr:this, QStringLiteral("节点颜色 · ") + stages[i],
          QColorDialog::DontUseNativeDialog);
      if (!color.isValid())
        return;
      timeline->colors[i] = color;
      settings.setValue(QString("color%1").arg(i), color.name());
      style();
      timeline->update();
    });
  }

  auto scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setWidget(timeline);
  scroll->viewport()->setAutoFillBackground(false);
  timeline->setAutoFillBackground(false);
  root->addWidget(scroll, 1);
  connect(timeline, &Timeline::scrollRequested, scroll, [scroll](int delta) {
    auto bar = scroll->verticalScrollBar();
    bar->setValue(bar->value() - delta);
  });
  auto footer = new QHBoxLayout;
  status = new QLabel;
  status->setObjectName("conferenceStatus");
  status->setWordWrap(true);
  status->setStyleSheet("font-size:10px;color:#d3d9d5");
  footer->addWidget(status, 1);
  footer->addLayout(legend);
  timeline->setToolTip(QStringLiteral("滚轮平移 · Ctrl + 滚轮缩放 · Shift + 滚轮上下滚动"));
  root->addLayout(footer);
  dataPath = storageDirectory() + "/conferences.json";
  load();
  timeline->refresh();
  restoreGeometry(settings.value("geometry").toByteArray());
  if (!settings.value("nodeLayoutV2", false).toBool()) {
    resize(560, 240);
    settings.setValue("nodeLayoutV2", true);
  }
  timeline->days = qBound(14, settings.value("days", 90).toInt(), 730);
  timeline->start = QDate::currentDate().addDays(-timeline->days / 4);
  auto desktop = new DesktopController(this);
  Q_UNUSED(desktop);
  setLocked(settings.value("locked", false).toBool());
  connect(lockButton, &QPushButton::toggled, this, &CalendarWindow::setLocked);
  connect(add, &QPushButton::clicked, this, [this] { edit(-1); });
  connect(close, &QPushButton::clicked, this, [this]{this->close();if(property("desktopExperiment").toBool())qApp->quit();});
  connect(timeline, &Timeline::editRequested, this, &CalendarWindow::edit);
  connect(timeline, &Timeline::reorderRequested, this,
          &CalendarWindow::reorder);
  auto timer = new QTimer(this);
  connect(timer, &QTimer::timeout, timeline, qOverload<>(&QWidget::update));
  timer->start(60000);
}
void CalendarWindow::setLocked(bool value) {
  locked = value;
  QSignalBlocker blocker(lockButton);
  lockButton->setChecked(value);
  lockButton->setIcon(lockIcon(value));
  lockButton->setAccessibleName(value?QStringLiteral("已锁定，点击解锁"):QStringLiteral("未锁定，点击锁定"));
  lockButton->setToolTip(value
                             ? QStringLiteral("窗口位置和大小已锁定，点击解锁")
                             : QStringLiteral("点击锁定窗口位置和大小"));
  settings.setValue("locked", value);
}
bool CalendarWindow::eventFilter(QObject *object, QEvent *event) {
  if (object->objectName() == "dragHeader" &&
      event->type() == QEvent::MouseButtonPress && !locked) {
    auto mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() == Qt::LeftButton && windowHandle()) {
      windowHandle()->startSystemMove();
      return true;
    }
  }
  return QWidget::eventFilter(object, event);
}
#ifdef Q_OS_WIN
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool CalendarWindow::nativeEvent(const QByteArray &, void *message,
                                 qintptr *result) {
#else
bool CalendarWindow::nativeEvent(const QByteArray &, void *message,
                                 long *result) {
#endif
  auto msg = static_cast<MSG *>(message);
  if (msg->message != WM_NCHITTEST)
    return false;
  if (locked) {
    *result = HTCLIENT;
    return true;
  }
  RECT r;
  GetWindowRect(reinterpret_cast<HWND>(winId()), &r);
  int px = GET_X_LPARAM(msg->lParam), py = GET_Y_LPARAM(msg->lParam),
      border = qRound(5 * devicePixelRatioF());
  bool left = px < r.left + border, right = px >= r.right - border,
       top = py < r.top + border, bottom = py >= r.bottom - border;
  *result = top      ? (left    ? HTTOPLEFT
                        : right ? HTTOPRIGHT
                                : HTTOP)
            : bottom ? (left    ? HTBOTTOMLEFT
                        : right ? HTBOTTOMRIGHT
                                : HTBOTTOM)
            : left   ? HTLEFT
            : right  ? HTRIGHT
                     : HTCLIENT;
  return true;
}
#endif
void CalendarWindow::reorder(int from, int to) {
  if (from < 0 || to < 0 || from >= timeline->conferences.size() ||
      to >= timeline->conferences.size() || from == to)
    return;
  auto old = timeline->conferences;
  timeline->conferences.move(from, to);
  if (!save())
    timeline->conferences = old;
  else
    timeline->selected = to;
  timeline->refresh();
}

bool CalendarWindow::load() {
  timeline->conferences.clear();
  QFile f(dataPath);
  if (!f.exists()) {
    status->setText(QStringLiteral("0 个会议"));
    return true;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    writable = false;
    status->setText(QStringLiteral("读取失败：") + f.errorString());
    return false;
  }
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(f.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject() ||
      (doc.object()["version"].toInt() != 1 &&
       doc.object()["version"].toInt() != 2) ||
      !doc.object()["conferences"].isArray()) {
    writable = false;
    status->setText(QStringLiteral("数据格式异常，已禁止覆盖。请备份并修复：") +
                    dataPath);
    return false;
  }
  for (auto v : doc.object()["conferences"].toArray()) {
    auto o = v.toObject();
    auto c = Conference::fromJson(o);
    bool valid = v.isObject() && !c.name.trimmed().isEmpty() &&
                 o["dates"].toArray().size() ==
                     (doc.object()["version"].toInt() == 1 ? 4 : 5);
    QDate last;
    for (int j = 0; j < 5; j++) {
      auto d = c.dates[j];
      if(!d.isValid() && ((doc.object()["version"].toInt()==1 && j==4) || (doc.object()["version"].toInt()==2 && o["dates"].toArray().at(j).isNull())))continue;
      if (!d.isValid() || (last.isValid() && d < last))
        valid = false;
      last = d;
    }
    if (!valid) {
      timeline->conferences.clear();
      writable = false;
      status->setText(QStringLiteral("会议记录异常，禁止覆盖：") + dataPath);
      return false;
    }
    timeline->conferences.append(c);
  }
  status->setText(
      QStringLiteral("%1 个会议").arg(timeline->conferences.size()));
  return true;
}
bool CalendarWindow::save() {
  if (!writable) {
    QMessageBox::warning(this, QStringLiteral("无法保存"),
                         QStringLiteral("原数据读取失败，请先修复文件：\n") +
                             dataPath);
    return false;
  }
  QDir().mkpath(QFileInfo(dataPath).absolutePath());
  QJsonArray a;
  for (const auto &c : timeline->conferences)
    a.append(c.json());
  QSaveFile file(dataPath);
  QByteArray bytes =
      QJsonDocument(QJsonObject{{"version", 2}, {"conferences", a}}).toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit()) {
    QMessageBox::warning(this, QStringLiteral("保存失败"), file.errorString());
    return false;
  }
  status->setText(QStringLiteral("%1 个会议 · 已保存 %2")
                      .arg(a.size())
                      .arg(QTime::currentTime().toString("HH:mm")));
  return true;
}
void CalendarWindow::edit(int index) {
  Conference c;
  if (index >= 0)
    c = timeline->conferences[index];
  else {
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

  }
  QDialog dialog(property("desktopAttached").toBool()?nullptr:this);
  dialog.setStyleSheet(
      "QWidget{background:#faf9f5;color:#293f3c} "
      "QPushButton{background:#fff;border:1px solid #cdd5c9} "
      "QTextEdit,QLineEdit,QDateEdit{background:white;color:#293f3c}");
  dialog.setWindowTitle(index < 0 ? QStringLiteral("新建会议")
                                  : QStringLiteral("编辑会议"));
  dialog.resize(450, 420);
  QFormLayout form(&dialog);
  QLineEdit name(c.name);
  form.addRow(QStringLiteral("会议名称 *"), &name);
  auto parseDate=[](QString text){text=text.trimmed();for(const auto &format:QStringList{"yyyy-MM-dd","yyyy/MM/dd","yyyyMMdd"}){auto d=QDate::fromString(text,format);if(d.isValid())return d;}return QDate();};
  QLineEdit *dates[5];
  for(int j=0;j<5;j++){
    dates[j]=new QLineEdit(c.dates[j].toString(Qt::ISODate),&dialog);dates[j]->setObjectName(QString("date%1").arg(j));dates[j]->setPlaceholderText(QStringLiteral("YYYY-MM-DD，可留空"));dates[j]->setClearButtonEnabled(true);
    auto row=new QWidget(&dialog);auto layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);layout->setSpacing(5);layout->addWidget(dates[j]);auto calendar=new QToolButton(row);calendar->setText(QStringLiteral("日历"));layout->addWidget(calendar);form.addRow(stages[j],row);
    connect(dates[j],&QLineEdit::returnPressed,&dialog,[&,j]{if(j<4)dates[j+1]->setFocus();else name.setFocus();});
    connect(calendar,&QToolButton::clicked,&dialog,[&,j,parseDate]{QDialog popup(&dialog);popup.setWindowTitle(stages[j]);auto layout=new QVBoxLayout(&popup);auto calendar=new QCalendarWidget(&popup);layout->addWidget(calendar);QDate seed=parseDate(dates[j]->text());for(int previous=j-1;!seed.isValid()&&previous>=0;previous--)seed=parseDate(dates[previous]->text());calendar->setSelectedDate(seed.isValid()?seed:QDate::currentDate());connect(calendar,&QCalendarWidget::clicked,&popup,[&](QDate date){dates[j]->setText(date.toString(Qt::ISODate));popup.accept();});popup.exec();});
  }
  QTextEdit note(c.note);
  note.setAcceptRichText(false);
  form.addRow(QStringLiteral("论文 / 备注"), &note);
  QLabel error;
  error.setStyleSheet("color:#b64e38");
  error.setWordWrap(true);
  form.addRow(&error);
  QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  if (index >= 0) {
    auto remove = buttons.addButton(QStringLiteral("删除会议"),
                                    QDialogButtonBox::DestructiveRole);
    connect(remove, &QPushButton::clicked, &dialog, [&] {
      if (QMessageBox::question(&dialog, QStringLiteral("删除会议"),
                                QStringLiteral("确定删除 %1？").arg(c.name)) !=
          QMessageBox::Yes)
        return;
      auto old = timeline->conferences;
      timeline->conferences.removeAt(index);
      if (!save()) {
        timeline->conferences = old;
        return;
      }
      timeline->selected = -1;
      timeline->refresh();
      dialog.reject();
    });
  }
  for(auto button:buttons.findChildren<QPushButton*>()){button->setAutoDefault(false);button->setDefault(false);}
  form.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    if (name.text().trimmed().isEmpty()) {
      error.setText(QStringLiteral("请填写会议名称。"));
      return;
    }
    QDate previous;
    for(int j=0;j<5;j++){
      if(dates[j]->text().trimmed().isEmpty())continue;
      QDate date=parseDate(dates[j]->text());if(!date.isValid()){error.setText(stages[j]+QStringLiteral("：请输入有效日期，例如 2026-11-15。"));dates[j]->setFocus();return;}
      if(previous.isValid()&&date<previous){error.setText(QStringLiteral("已填写的日期须按摘要、正文、审稿、回复、最终结果顺序排列。"));dates[j]->setFocus();return;}previous=date;
    }
    dialog.accept();
  });
  if (dialog.exec() != QDialog::Accepted)
    return;
  c.name = name.text().trimmed();
  c.note = note.toPlainText();
  for (int j = 0; j < 5; ++j)
    c.dates[j] = parseDate(dates[j]->text());
  auto old = timeline->conferences;
  if (index < 0) {
    timeline->conferences.append(c);
    index = timeline->conferences.size() - 1;
  } else
    timeline->conferences[index] = c;
  if (!save()) {
    timeline->conferences = old;
    return;
  }
  timeline->selected = index;
  timeline->refresh();
}
void CalendarWindow::closeEvent(QCloseEvent *e) {
  settings.setValue("geometry", saveGeometry());
  settings.setValue("days", timeline->days);
  settings.remove("start");
  QWidget::closeEvent(e);
  if(e->isAccepted()&&property("desktopExperiment").toBool())qApp->quit();
}

void CalendarWindow::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(48, 48, 48, 128));
}

void CalendarWindow::recreateDesktopWindow(){auto geometry=this->geometry();hide();destroy();setGeometry(geometry);show();}
