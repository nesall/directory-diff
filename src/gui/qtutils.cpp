#include "qtutils.h"

#include <QUrl>
#include <QUrlQuery>
#include <QFileInfo>
#ifndef NO_GUI
#include <QPainter>
#include <QTreeView>
#include <QHeaderView>
#include <QBoxLayout>
#include <QSortFilterProxyModel>
#include <QAction>
#include <QToolButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDebug>
#include <QDesktopServices>
#include <QMessageBox>
#include <QColorDialog>
#include <QRgb>
#endif

#include <fstream>
#include <string>
#include <mutex>

#ifndef ASSERT
#include <cassert>
#define ASSERT assert
#endif

#ifndef NO_GUI
namespace {
  
  QModelIndexList getIndexesFromRange(const QItemSelectionRange& range)
  {
    QModelIndexList result;
    const QAbstractItemModel *m = range.model();
    if (m && range.isValid()) {
      for (int column = range.left(); column <= range.right(); column++) {
        for (int row = range.top(); row <= range.bottom(); row++) {
          const QModelIndex index = m->index(row, column, range.parent());
          const Qt::ItemFlags flags = m->flags(index);
          if ((flags & Qt::ItemIsSelectable) && (flags & Qt::ItemIsEnabled)) {
            result.append(index);
          }
        }
      }
    }
    return result;
  }

  QModelIndexList getSelectionIndexes(const QItemSelection &ranges)
  {
    QModelIndexList res;
    for (QList<QItemSelectionRange>::ConstIterator it = ranges.constBegin(); it != ranges.constEnd(); ++it) {
      res += getIndexesFromRange(*it);
    }
    return res;
  }

  QToolButton *toolButton(const QString &title, const QString &tooltip) {
    QToolButton *t = new QToolButton;
    t->setText(title);
    t->setToolTip(tooltip);
    t->setAutoRaise(true);
    return t;
  }
} // anonymous namespace


#if 1
QInfoLineEdit::QInfoLineEdit(QWidget *p) : QLineEdit(p)
{
  HookKey *h = new HookKey(this, Qt::Key_Escape);
  connect(h, SIGNAL(hooked()), this, SLOT(escHooked()));
}

void QInfoLineEdit::paintEvent(QPaintEvent *e)
{
  QLineEdit::paintEvent(e);
  QPainter p(this);
  p.save();
  if (text().isEmpty() && !hasFocus()) {
    p.setPen(Qt::darkGray);
    QFont f(p.font());
    f.setItalic(true);
    p.setFont(f);
    p.drawText(rect(), Qt::AlignCenter, infoText());
  }
  p.restore();
}

void QInfoLineEdit::setInfoText(const QString &info)
{
  infoText_ = info;
  repaint();
}

void QInfoLineEdit::escapeKeyInvalidatesText(bool f)
{
  escapeInvalidates_ = f;
}

void QInfoLineEdit::escHooked()
{
  if (escapeInvalidates_) {
    setText(QString());
    clearFocus();
  }
}
#endif

#endif // NO_GUI

///////////////////////////////////////////////////////////////////////////////////////////////

QString msToLabel(long ms)
{
  return QString("%1:%2").arg(ms / 60000, 2, 10, QChar('0'))
    .arg((ms % 60000) / 1000, 2, 10, QChar('0'));
}

QString doubleToLabel(double v, int decimals, const QString &suffix)
{
  QString s("%L1");
  s = s.arg(v, 0, 'f', decimals);
  s += suffix;
  return s;
}

#ifndef NO_GUI
QColor rgbToColor(const QString &rgb)
{
  QColor c;
  if (rgb.startsWith("rgb")) {
    QString s(rgb.mid(3).data());
    s.replace("(", "");
    s.replace(")", "");
    auto v = s.split(',');
    if (v.length() == 3) {
      c.setRed(v[0].toInt());
      c.setGreen(v[1].toInt());
      c.setBlue(v[2].toInt());
    }
  }
  return c;
}

QColor strToColor(const QString &s)
{
  auto c = rgbToColor(s);
  if (!c.isValid()) {
    c.setNamedColor(s);
  }
  return c;
}
#endif

QString qString(const std::string & s)
{
  return QString(QByteArray(s.c_str(), static_cast<int>(s.length())));
}

QByteArray qByteArray(const std::string & s)
{
  return QByteArray(s.c_str(), static_cast<int>(s.size()));
}

QDateTime dateStrToDateTime(const std::string &dateStr) {
  return 0 < dateStr.find('-', 0) ? QDateTime::fromString(dateStr.c_str(), "yyyy-MM-dd") : QDateTime::fromString(dateStr.c_str(), "yyyyMMdd");
}

QDateTime dateStrToDateTime(const std::string &dateStr, const std::string &timeStr) {
  std::string s = dateStr + " " + timeStr;
  //std::string t = "20181219 09:30";
  return /*0 < dateStr.find('-', 0) ? QDateTime::fromString(s.c_str(), "yyyy-MM-dd HH:mm") : */QDateTime::fromString(s.c_str(), "yyyyMMdd HH:mm");
}

QString toSepString(double v, bool abbr)
{
  QString s = QString::number(v, 'f', 1);
  int u = 0;
  if (5 < s.length()) {
    s = QString::number(v, 'f', 0);
    u = 2;
  }

  if (abbr) {
    if (11 - u < s.length()) {
      return s.left(s.length() - (11 - u)) + "." + s.at(s.length() - (11 - u)) + " B";
    } else if (8 - u < s.length()) {
      return s.left(s.length() - (8 - u)) + "." + s.at(s.length() - (8 - u)) + " M";
    }
  }

  if (5 - u < s.length()) {
    s.insert(s.length() - (5 - u), ',');
  }
  if (9 - u < s.length()) {
    s.insert(s.length() - (9 - u), ',');
  }
  if (13 - u < s.length()) {
    s.insert(s.length() - (13 - u), ',');
  }
  if (17 - u < s.length()) {
    s.insert(s.length() - (17 - u), ',');
  }
  return s;
}

std::vector<std::string> stdStringList(const QStringList & list)
{
  std::vector<std::string> v;
  for (const auto &s : list) {
    v.push_back(s.toStdString());
  }
  return v;
}

std::map<std::string, std::string> stdMap(const std::map<QString, QString>& m)
{
  std::map<std::string, std::string> res;
  for (const auto &k : m) {
    res[k.first.toStdString()] = k.second.toStdString();
  }
  return res;
}

void registerAsyncQThread(QThread *p)
{
  //theApp().registerQThread(p);
}


///////////////////////////////////////////////////////////////////////////////////////////////

#ifndef NO_GUI

IconRef::IconRef(const char *name, int w, int h, ColorMode mode) : name_(name), w_(w), h_(h), mode_(mode)
{
}

IconRef::IconRef(const QString &name, int w, int h, ColorMode mode) : name_(name), w_(w), h_(h), mode_(mode)
{
}

IconRef::IconRef(const QPixmap &icon, int w, int h, ColorMode mode) : px_(icon), w_(w), h_(h), mode_(mode)
{
}

bool IconRef::isNull() const
{
  return px_.isNull() && name_.isEmpty();
}

void IconRef::apply(QObject *target) const
{
  ASSERT(target);
  if (!px_.isNull()) {
    ThemeIcon::setup(target, IconRef(px_, w_, h_));
  } else if (!name_.isEmpty()) {
    ThemeIcon::setup(target, IconRef(name_, w_, h_));
  }
}


///////////////////////////////////////////////////////////////////////////////////////////////


ThemeIcon::ThemeIcon(QObject *target, const IconRef &iref) : QObject(target), iconRef_(iref), target_(target)
{
  updateIcon();
  if (theApp().mainWindow()) {
    QObject::connect(theApp().mainWindow(), &MainWindow::themeChanged, this, [this]() { updateIcon(); });
  }
}

bool ThemeIcon::isDarkTheme()
{
  if (auto w = theApp().mainWindow()) {
    return w->isDarkTheme();
  }
  return false;
}

QString ThemeIcon::themedPostfix()
{
  return isDarkTheme() ? "-light" : "-dark";
}

void ThemeIcon::updateIcon()
{
  if (!theApp().mainWindow()) {
    //LOG_MSG << "ThemeIcon::get GUI not ready yet." << name_;
    QObject::connect(theApp().postman(), &QAppPostman::guiReady, this, [this]() { 
      updateIcon(); 
      QObject::connect(theApp().mainWindow(), &MainWindow::themeChanged, this, [this]() { updateIcon(); });
    });
  } 
  if (target_) {
    auto px = fromIconRef(iconRef_);
    if (auto t = qobject_cast<QToolButton *>(target_)) {
      t->setIcon(px);
    } else if (auto t = qobject_cast<QAction *>(target_)) {
      t->setIcon(px);
    } else if (auto t = qobject_cast<watchlist::QWatchlistItem *>(target_)) {
      t->setIcon(px);
    } else if (auto t = qobject_cast<QWidget *>(target_)) {
      t->setWindowIcon(px);
    }
  }
}

QPixmap ThemeIcon::fromIconRef(const IconRef &iref)
{
  if (!iref.px_.isNull()) {
    return iref.px_;
  }
  return fromName(iref.name_, iref.w_, iref.h_);
}

QPixmap ThemeIcon::fromName_p(const QString &name, ColorMode mode)
{
  QPixmap px = theApp().findIcon(name + themedPostfix());
  if (px.isNull()) {
    QFile file(":/" + name);
    if (file.open(QFile::ReadOnly)) {
      auto bs = file.readAll();
      if (!bs.isEmpty()) {
        QString sbs(bs);
        auto clr = theApp().palette().color(QPalette::WindowText);
        clr = colorForMode(mode, contrasting(clr, 0.1));
        //clr = QColor(clr.red(), clr.green(), clr.blue(), 125);
        //clr = clr.contr
        const auto cname = clr.name();
        //clr = QColor(clr.rgb() ^ 0x00FFFFFF); // inverting
        sbs.replace("fill", "_fill").replace("<path ", "<path fill=\"" + cname + "\" ");
        sbs.replace("stroke=\"#000000\"", "stroke=\"" + cname + "\" ");
        sbs.replace("stroke:#000000", "stroke:" + cname);
        sbs.replace("_fill:#000000", "fill:" + cname);
        sbs.replace("fill:#000000", "fill:" + cname);
        sbs.replace("<text", "<text fill=\"" + cname + "\"");

        px.loadFromData(sbs.toUtf8(), "svg");
      }
    }
  }
  if (px.isNull()) {
    px = theApp().findIcon(name);
  }
  return px;
}

QPixmap ThemeIcon::fromName(const QString &name, int w, int h, const QPixmap &pxAlt, ColorMode mode)
{
  if (name.isEmpty()) return pxAlt;
  QString s = name;
  if (w && h) {
    s += QString("%1x%2").arg(w).arg(h);
  } else if (w) {
    s += QString("%1x%2").arg(w).arg(0);
  } else if (h) {
    s += QString("%1x%2").arg(0).arg(h);
  }
  QPixmap px = theApp().cachedIcon(s);
  if (px.isNull()) {
    px = fromName_p(name, mode);
    if (!px.isNull()) {
      if (w && h) {
        px = px.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      } else if (w) {
        px = px.scaledToWidth(w, Qt::SmoothTransformation);
      } else if (h) {
        px = px.scaledToHeight(h, Qt::SmoothTransformation);
      }
    }
    if (!px.isNull()) {
      theApp().addCachedIcon(s, px);
    }
  }
  if (px.isNull()) {
    px = pxAlt;
  }
  return px;
}

void ThemeIcon::setup(QObject *target, const IconRef &iref)
{
  if (!iref.isNull() && target) {
    auto v = target->findChildren<ThemeIcon *>();
    for (ThemeIcon *t : v) {
      if (t->parent() == target) { // immediate children only
        t->deleteLater();
      }
    }
    new ThemeIcon(target, iref);
  }
}


///////////////////////////////////////////////////////////////////////////////////////////////


double isDarkColor(const QColor &clr)
{
  return perceivedBrightness(clr) < 0.5;
}

double perceivedBrightness(const QColor &clr)
{
  return sqrt(0.241 * std::powf(clr.redF(), 2.f) + 0.691 * std::powf(clr.greenF(), 2.f) + 0.068 * std::powf(clr.blueF(), 2.f));
}

QColor contrasting(const QColor &clr, double amount)
{
  ASSERT(0 <= amount && amount <= 1.);
  auto c = isDarkColor(clr) ? QColor("white") : QColor("black");
  c.setAlphaF(amount);
  return overlaidWith(clr, c);
}

QColor overlaidWith(const QColor &clr, const QColor &src)
{
  const auto destAlpha = clr.alpha();
  if (destAlpha <= 0)
    return src;
  const auto invA = 0xff - (int)src.alpha();
  const auto resA = 0xff - (((0xff - destAlpha) * invA) >> 8);
  if (resA <= 0)
    return clr;
  const auto da = (invA * destAlpha) / resA;
  return QColor(
    (src.red() + ((((int)clr.red() - src.red()) * da) >> 8)),
    (src.green() + ((((int)clr.green() - src.green()) * da) >> 8)),
    (src.blue() + ((((int)clr.blue() - src.blue()) * da) >> 8)),
    resA);
}

QColor colorForMode(ColorMode mode, const QColor &def)
{
  QColor clr{ def };
  if (!def.isValid())
    clr = theApp().palette().color(QPalette::WindowText);
  switch (mode) {
  case ColorMode::Red:
    return isDarkColor(clr) ? QColor("darkRed") : QColor("red").lighter(125);
  case ColorMode::Red1:
    return isDarkColor(clr) ? QColor("darkRed") : QColor("tomato");
  case ColorMode::Green:
    return isDarkColor(clr) ? QColor("darkGreen") : QColor("green").lighter();
  default:
    break;
  }
  return def;
}

void addActionToWidget(QWidget * owner, QAction *& act, const QString & title, const QIcon & px, const QKeySequence & keyseq)
{
  act = new QAction(QIcon(px), title, owner);
  act->setShortcut(keyseq);
  act->setShortcutContext(Qt::WidgetShortcut);
  if (!act->shortcut().isEmpty()) {
    act->setToolTip(QString("%1 (%2)").arg(act->text(), act->shortcut().toString()));
  }
  owner->addAction(act);
}

void addActionToWidget(QWidget *owner, QAction *&act, const QString &title, const IconRef &iid, const QKeySequence &keyseq)
{
  addActionToWidget(owner, act, title, QIcon(), keyseq);
  iid.apply(act);
}



QToolButton *createToolbutton(QAction *action)
{
  QToolButton *p = new QToolButton;
  p->setDefaultAction(action);
  p->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  p->setAutoRaise(true);
  return p;
}

QToolButton * createToolbutton(const QString & title, const QIcon & icon, const QString &tooltip)
{
  QToolButton *t = toolButton(title, tooltip);
  t->setToolButtonStyle(title.isEmpty() ? Qt::ToolButtonStyle::ToolButtonIconOnly : 
    icon.isNull() ? Qt::ToolButtonStyle::ToolButtonTextOnly : Qt::ToolButtonStyle::ToolButtonTextBesideIcon);
  t->setIcon(icon);
  return t;
}

QToolButton *createToolbutton(const QString &title, const IconRef &iid, const QString &tooltip)
{
  QToolButton *t = toolButton(title, tooltip);
  t->setToolButtonStyle(title.isEmpty() ? Qt::ToolButtonStyle::ToolButtonIconOnly : Qt::ToolButtonStyle::ToolButtonTextBesideIcon);
  iid.apply(t);
  return t;
}

void restoreSplitterSizes(QSplitter * splitter, const QString & key)
{
  QVariant vSizes = QSettings().value(key);
  if (!vSizes.isNull()) {
    QStringList v = vSizes.toString().split(';', Qt::SkipEmptyParts);
    QList<int> list;
    for (int j = 0; j < v.size(); j++) {
      list.append(v[j].toInt());
    }
    splitter->setSizes(list);
  }
}

void setToSaveSplitterSizes(QSplitter * splitter, const QString & key)
{
  QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [splitter, key](int, int) {
    QList<int> sz = splitter->sizes();
    QString s;
    for (int j : sz) {
      s += QString::number(j) + ";";
    }
    QSettings().setValue(key, s);
  });
}


///////////////////////////////////////////////////////////////////////////////////////////////

QListFilterWidget::QListFilterWidget(QWidget *p)
  : Super(p)
{
  class MyTreeView : public QTreeView {
    QLineEdit *lbl_;
    QVariant &data_;
    QListFilterWidget *w_;
  public:
    MyTreeView(QLineEdit *t, QVariant &data, QListFilterWidget *w) : lbl_(t), data_(data), w_(w) {
      setUniformRowHeights(true);
      setAlternatingRowColors(true);
      setRootIsDecorated(false);
      header()->setVisible(true);
    }
  private:
    virtual void currentChanged(const QModelIndex &current, const QModelIndex &previous) {
      QTreeView::currentChanged(current, previous); // TOTEST:
      if (current.isValid() && selectionMode() == SingleSelection) {
        const QAbstractItemModel *m = current.model();
        QString s = m->data(current).toString();
        if (const QSortFilterProxyModel *pm = qobject_cast<const QSortFilterProxyModel *>(m)) {
          QStandardItemModel *sm = static_cast<QStandardItemModel *>(const_cast<QSortFilterProxyModel *>(pm)->sourceModel());
          Q_ASSERT(sm);
          QModelIndex srcIndex = pm->mapToSource(current);
          Q_ASSERT(srcIndex.isValid());
          if (sm->invisibleRootItem() && sm->invisibleRootItem()->child(srcIndex.row())) {
            data_ = sm->invisibleRootItem()->child(srcIndex.row())->data();
          }
        }
        lbl_->setText(s);
      }
    }
    virtual void selectionChanged(const QItemSelection &s, const QItemSelection &d) {
      QTreeView::selectionChanged(s, d);
      if (selectionMode() != SingleSelection && selectionMode() != NoSelection) {
        QModelIndexList indexes = getSelectionIndexes(selectionModel()->selection());
        lbl_->setText(QString("%1 item(s) selected").arg(indexes.size()));
      }
    }
    virtual void contextMenuEvent(QContextMenuEvent *e) override {
      auto index = indexAt(e->pos());
      if (index.isValid() && w_->itemsDeletable()) {
        QMenu menu;
        menu.addAction("Delete item", [this, index]() {
          Q_EMIT w_->itemDeleteRequested(index.row());
        });
        menu.exec(e->globalPos());
      }
    }
  };

  class MyTreeModel : public QStandardItemModel {
  public:
    MyTreeModel(QListFilterWidget *f) : QStandardItemModel(0, 0), f_(f) {
      //QList<QStandardItem *> headers;
      //headers.append(new QStandardItem(f->columnTitle()));
      //this->appendColumn(headers);
    }
  private:
    QListFilterWidget *f_;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const {
      if (0 == section && Qt::DisplayRole == role) {
        return f_->columnTitle();
      }
      return QStandardItemModel::headerData(section, orientation, role);
    }
    virtual Qt::ItemFlags flags(const QModelIndex &) const {
      return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }
  };
  QBoxLayout *vl = new QVBoxLayout(this);
  vl->setSpacing(1);
  vl->setContentsMargins(1, 1, 1, 1);
  QInfoLineEdit *t = new QInfoLineEdit;
  t->setPlaceholderText("Type to filter");
  selection_ = new QLineEdit;
  selection_->setReadOnly(true);
  selection_->setEnabled(false);
  vl->addWidget(filter_ = t);
  vl->addWidget(list_ = new MyTreeView(selection_, selData_, this));
  vl->addWidget(selection_);
  //vl->addStretch();
  QSortFilterProxyModel *pm = new QSortFilterProxyModel;
  list_->setModel(pm);
  pm->setSourceModel(new MyTreeModel(this));
  selection_->setText("< no item selected >");
  list_->setFocus();
  pm->setFilterCaseSensitivity(Qt::CaseInsensitive);
  connect(filter_, SIGNAL(textChanged(const QString &)), this, SLOT(filterChanged(const QString &)));
  connect(selection_, SIGNAL(textChanged(const QString &)), this, SIGNAL(selectionChanged(const QString &)));
  connect(list_, SIGNAL(doubleClicked(const QModelIndex &)), this, SIGNAL(doubleClicked(const QModelIndex &)));

  //struct HookEsc : public QObject {
  //  QLineEdit *le_;
  //  HookEsc(QLineEdit *le) : QObject(le), le_(le) {
  //    le->installEventFilter(this);
  //  }
  //  bool eventFilter(QObject *obj, QEvent *ev) {
  //    if (obj == le_) {
  //      if (QEvent::KeyPress == ev->type()) {
  //        QKeyEvent *ke = static_cast<QKeyEvent *>(ev);
  //        if (ke->key() == Qt::Key_Escape) {
  //          le_->setText(QString());
  //          le_->clearFocus();
  //          return true;
  //        }
  //      }
  //    }
  //    return QObject::eventFilter(obj, ev);
  //  }
  //};
  //new HookEsc(filter_);
  t->escapeKeyInvalidatesText(true);
}

void QListFilterWidget::setSelectedItem(int j)
{
  list_->setCurrentIndex(list_->model()->index(j, 0));
}

void QListFilterWidget::setFilterHint(const QString &hint)
{
  static_cast<QInfoLineEdit *>(filter_)->setInfoText(hint);
}

void QListFilterWidget::setListItems(const QStringList &vec)
{
  clear();
  for (int j = 0; j < vec.size(); j++) {
    //QStandardItem *item = new QStandardItem(qString(vec[j]));
    //static_cast<QStandardItemModel *>(m)->invisibleRootItem()->appendRow(item);
    addItem(vec[j]);
  }
  QSortFilterProxyModel *proxy = static_cast<QSortFilterProxyModel *>(list_->model());
  QAbstractItemModel *m = proxy->sourceModel();
  if (0 < m->rowCount()) {
    list_->setCurrentIndex(m->index(0, 0));
    list_->selectionModel()->select(m->index(0, 0), QItemSelectionModel::Rows);
  }
  proxy->invalidate();
  filter_->clearFocus();
  list_->setFocus();
}

void QListFilterWidget::addItem(const QString &s, const QVariant &data)
{
  QAbstractItemModel *m = static_cast<QSortFilterProxyModel *>(list_->model())->sourceModel();
  QStandardItem *item = new QStandardItem(s);
  item->setData(data);
  static_cast<QStandardItemModel *>(m)->invisibleRootItem()->appendRow(item);
}

void QListFilterWidget::addItem(QStandardItem *item)
{
  Q_ASSERT(item);
  QAbstractItemModel *m = static_cast<QSortFilterProxyModel *>(list_->model())->sourceModel();
  static_cast<QStandardItemModel *>(m)->invisibleRootItem()->appendRow(item);
}

QStandardItem *QListFilterWidget::takeItem(int row)
{
  QSortFilterProxyModel *proxy = static_cast<QSortFilterProxyModel *>(list_->model());
  QModelIndex ind = proxy->mapToSource(proxy->index(row, 0));
  Q_ASSERT(ind.isValid());
  QStandardItemModel *m = static_cast<QStandardItemModel *>(proxy->sourceModel());
  QList<QStandardItem *> v = m->takeRow(ind.row());
  if (!v.isEmpty()) {
    return v.first();
  }
  return 0;
}

QString QListFilterWidget::itemAt(int row, QVariant *data) const
{
  QSortFilterProxyModel *proxy = static_cast<QSortFilterProxyModel *>(list_->model());
  QModelIndex ind = proxy->mapToSource(proxy->index(row, 0));
  Q_ASSERT(ind.isValid());
  QStandardItemModel *sm = static_cast<QStandardItemModel *>(proxy->sourceModel());
  Q_ASSERT(sm);
  QStandardItem *t = sm->item(row);
  Q_ASSERT(t);
  if (data) {
    *data = t->data();
  }
  return t->text();
}

void QListFilterWidget::filterChanged(const QString &s)
{
  static_cast<QSortFilterProxyModel *>(list_->model())->setFilterWildcard(s);
}

QString QListFilterWidget::selectedItem() const
{
  return selection_->text();
}

void QListFilterWidget::clear()
{
  QSortFilterProxyModel *proxy = static_cast<QSortFilterProxyModel *>(list_->model());
  QStandardItemModel *m = static_cast<QStandardItemModel *>(proxy->sourceModel());
  proxy->invalidate();
  m->clear();
  selection_->setText("< no item selected >");
}

void QListFilterWidget::setItemsDeletable(bool f)
{
  itemsDeletable_ = f;
}


///////////////////////////////////////////////////////////////////////////////////////////////


QListInputDialog::QListInputDialog(QWidget * p) : QDialog(p)
{
  setWindowTitle("Input List");
  QBoxLayout *hl = new QVBoxLayout(this);
  hl->addWidget(new QLabel("Insert items each on a new line or separate them by comma, semi-colon or space"));
  edit_ = new QPlainTextEdit;
  edit_->setPlaceholderText("DDM, UDOW, TQQQ");
  hl->addWidget(edit_);
  QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
  hl->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QStringList QListInputDialog::getList() const
{
  QString s = edit_->toPlainText();
  auto res = s.split(QRegularExpression("[,\\s\\n;]"), Qt::SkipEmptyParts);
  return res;
}


///////////////////////////////////////////////////////////////////////////////////////////////


QBaseComboBox::QBaseComboBox(QWidget *parent) : Super(parent)
{
  view()->installEventFilter(this);
}

void QBaseComboBox::focusInEvent(QFocusEvent *e)
{
  Super::focusInEvent(e);
  if (popupOnFocus_) {
    switch (e->reason()) {
    case Qt::TabFocusReason:
    case Qt::BacktabFocusReason:
    case Qt::OtherFocusReason:
      showPopup();
      break;
    default:
      break;
    }
  }
}

bool QBaseComboBox::eventFilter(QObject *obj, QEvent *e)
{
  switch (e->type()) {
  case QEvent::KeyPress:
  {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(e);
    if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
      if (view() == obj && view()->isVisible()) {
        QModelIndex index = view()->currentIndex();
        setCurrentIndex(index.row());
        hidePopup();
        focusNextChild();
      } else if (lineEdit() == obj && isEditable()) {
        //lineEdit()->clearFocus();
        //static_cast<QBaseLineEdit *>(lineEdit())->focusNextChild();
      }
      return false;
    }
  }
  break;
  case QEvent::Show:
    if (view() == obj) emit popupAboutToBeShown();
    break;
  case QEvent::Hide:
    if (view() == obj) emit popupAboutToBeHidden();
    break;
  default:
    break;
  }
  return Super::eventFilter(obj, e);
}

void QBaseComboBox::showPopup()
{
  emit popupRequested();
  Super::showPopup();
}

void QBaseComboBox::showPopupOnFocus(bool on)
{
  popupOnFocus_ = on;
}


///////////////////////////////////////////////////////////////////////////////////////////////


QDelayedTask::QDelayedTask(QObject *p) : QObject(p)
{
}

QDelayedTask::~QDelayedTask()
{
  for (auto it = tasks_.cbegin(); it != tasks_.cend(); ++ it) {
    if (it->second.timer_)
      it->second.timer_->stop();
    if (it->second.func_)
      it->second.func_();
  }
}

void QDelayedTask::addTask(int id, std::function<void()> f, int ms)
{
  const auto it = tasks_.find(id);
  if (it != tasks_.cend()) {
    auto &p = it->second;
    ASSERT(p.timer_);
    p.func_ = f;
    p.timer_->stop();
    p.timer_->setInterval(ms);
    p.timer_->start();
    tasks_[id] = p;
  } else {
    Task p;
    p.func_ = f;
    p.timer_ = new QTimer(this);
    p.timer_->setInterval(ms);
    p.timer_->setProperty("timer-id", id);
    p.timer_->setSingleShot(true);
    connect(p.timer_, SIGNAL(timeout()), this, SLOT(onTimer()));
    p.timer_->start();
    tasks_.insert({ id, p });
  }
}

void QDelayedTask::cancelTask(int id)
{
  Task p;
  const auto it = tasks_.find(id);
  if (it != tasks_.cend()) {
    ASSERT(p.timer_);
    auto &p = it->second;
    p.timer_->stop();
    tasks_.erase(id);
  }
}

void QDelayedTask::commitTaskNow(int id)
{
  const auto it = tasks_.find(id);
  if (it != tasks_.cend()) {
    auto &p = it->second;
    ASSERT(p.timer_ && p.func_);
    p.timer_->stop();
    p.func_();
    tasks_.erase(id);
  }
}

void QDelayedTask::onTimer()
{
  if (auto t = qobject_cast<QTimer *>(sender())) {
    int id = t->property("timer-id").toInt();
    commitTaskNow(id);
  }
}


///////////////////////////////////////////////////////////////////////////////////////////////


void QErrorReportLineEdit::paintEvent(QPaintEvent *e)
{
  Super::paintEvent(e);
  if (errorMode()) {
    QPainter p(this);
    p.setPen(colorError_);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
  }
}


///////////////////////////////////////////////////////////////////////////////////////////////


namespace {

  struct IClickListener {
    virtual void clicked(QWidget *section) = 0;
  };

  class QHeaderFrame : public QFrame {
    class QHeaderIconWidget : public QWidget {
    public:
      QHeaderIconWidget() {
        setMinimumWidth(16);
        setMaximumWidth(16);
        setMinimumHeight(16);
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
      }
      QStyle::StandardPixmap pixmap_ = QStyle::StandardPixmap::SP_ArrowUp;
    private:
      void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        QPixmap px = style()->standardPixmap(pixmap_);
        px = px.scaledToHeight(height());
        p.drawPixmap(QRect(0, 0, px.width(), px.height()), px);

      }
    };
    QList<IClickListener *> listeners_;
    QHeaderIconWidget *hi_ = nullptr;
  public:
    QHeaderFrame(const QString &title, const QPixmap &icon) {
      setCursor(Qt::PointingHandCursor);
      hi_ = new QHeaderIconWidget;
      QHBoxLayout *hl = new QHBoxLayout(this);
      hl->addWidget(hi_);
      QLabel *li = new QLabel;
      li->setPixmap(icon);
      hl->addWidget(li);
      li->setVisible(!icon.isNull());
      QLabel *l = new QLabel(title);
      hl->addWidget(l);
      l->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
      setFrameStyle(QFrame::Panel | QFrame::Raised);
    }
    void addClickListener(IClickListener *l) {
      listeners_.append(l);
    }
    void setExpandedState(bool f) {
      hi_->pixmap_ = f ? QStyle::StandardPixmap::SP_ArrowDown : QStyle::StandardPixmap::SP_ArrowRight;
      hi_->repaint();
    }
  private:
    void mouseReleaseEvent(QMouseEvent *e) override {
      QFrame::mouseReleaseEvent(e);
      for (auto l : listeners_) {
        l->clicked(this);
      }
    }
  };

  class QSectionFrame : public QFrame, IClickListener {
    QHeaderFrame *headerFrame_ = nullptr;
    QWidget *contentWrapper_ = nullptr;
    QList<IClickListener *> listeners_;
  public:
    QSectionFrame(QHeaderFrame *header, QWidget *contentWrapper) : headerFrame_(header), contentWrapper_(contentWrapper) {
      QVBoxLayout *vl = new QVBoxLayout(this);
      vl->setContentsMargins(0, 0, 0, 0);
      vl->setSpacing(0);
      vl->addWidget(header);
      vl->addWidget(contentWrapper);
      vl->addStretch();
      headerFrame_->addClickListener(this);
      setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
      setExpanded(false);
    }
    void addClickListener(IClickListener *l) {
      listeners_.append(l);
    }
    void setExpanded(bool f) {
      contentWrapper_->setVisible(f);
      headerFrame_->setExpandedState(f);
    }
    bool isExpanded() const {
      return contentWrapper_->isVisible();
    }
  private:
    void clicked(QWidget *) override {
      for (auto l : listeners_) {
        l->clicked(this);
      }
    }
  };

} // anonymous namespace

struct QAccordionWidget::Impl : public IClickListener {
  QVBoxLayout *vl_;
  QList<QWidget *> headerWidgets_;
  QList<QWidget *> contentWidgets_;
  QList<QWidget *> sectionWidgets_;
  int expandedSection_ = -1;

  QAccordionWidget *accrd_ = nullptr;

  void clicked(QWidget *w) override {
    int k = sectionWidgets_.indexOf(w);
    Q_ASSERT(k != -1);
    QSectionFrame *sw = static_cast<QSectionFrame *>(w);
    bool f = sw->isExpanded();
    if (!f) {
      expand(k);
    }
  }

  void expand(int k) {
    QSectionFrame *sw = static_cast<QSectionFrame *>(sectionWidgets_[k]);
    if (sw->isExpanded()) {
      return;
    }
    for (auto t : sectionWidgets_) {
      static_cast<QSectionFrame *>(t)->setExpanded(false);
    }
    expandedSection_ = k;
    sw->setExpanded(true);
    Q_EMIT accrd_->sectionExpanded(k);
  }
};

QAccordionWidget::QAccordionWidget(QWidget *p) : QWidget(p), impl_(new Impl)
{
  impl_->accrd_ = this;
  impl_->vl_ = new QVBoxLayout(this);
  impl_->vl_->setContentsMargins(0, 0, 0, 0);
}

void QAccordionWidget::addSection(const QString &title, const QPixmap &icon)
{
  addSection(new QHeaderFrame(title, icon));
}

void QAccordionWidget::addSection(QWidget *header)
{
  QWidget *contentWrapper = new QWidget;
  QWidget *contentDummy = new QWidget;
  QVBoxLayout *vlWrapper = new QVBoxLayout(contentWrapper);
  vlWrapper->addWidget(contentDummy);
  QSectionFrame *sw = new QSectionFrame(static_cast<QHeaderFrame *>(header), contentWrapper);
  impl_->vl_->addWidget(sw);
  impl_->headerWidgets_.append(header);
  impl_->contentWidgets_.append(contentDummy);
  impl_->sectionWidgets_.append(sw);
  sw->addClickListener(impl_.get());
}

void QAccordionWidget::setContentWidget(int j, QWidget *contentWidget)
{
  Q_ASSERT(j < sectionCount());
  Q_ASSERT(contentWidget);
  QWidget *old = impl_->contentWidgets_[j];
  QWidget *wrapper = old->parentWidget();
  auto *vl = wrapper->layout();
  vl->replaceWidget(old, contentWidget);
  old->deleteLater();
  impl_->contentWidgets_[j] = contentWidget;
}

int QAccordionWidget::sectionCount() const
{
  return impl_->sectionWidgets_.size();
}

void QAccordionWidget::wrapUp()
{
  impl_->vl_->addStretch();
}

void QAccordionWidget::setExpandedSection(int j)
{
  impl_->expand(j);
}


///////////////////////////////////////////////////////////////////////////////////////////////

namespace {
  QPixmap colorPx(const QColor &c) {
    QPixmap px(16, 16);
    QPainter p(&px);
    p.setPen(QColor("black"));
    if (!c.isValid()) {
      p.setBrush(QColor("white"));
    } else {
      p.setBrush(c);
    }
    p.drawRect(px.rect().adjusted(0, 0, -1, -1));
    if (!c.isValid()) {
      p.drawText(px.rect(), Qt::AlignCenter, "A");
    }
    return px;
  }
} // anonymous namespace

QColorButton::QColorButton(const QColor &c, QWidget *p) : QToolButton(p)
{
  setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonIconOnly);
  setColor(c);
  QObject::connect(this, &QToolButton::clicked, this, [this]() {
    auto c = QColorDialog::getColor(color(), this, "Pick a color");
    if (c.isValid()) {
      setColor(c);
      Q_EMIT colorChanged();
    }
  });
}

void QColorButton::setColor(const QColor &c)
{
  if (clr_ != c) {
    clr_ = c;
    setIcon(QIcon(colorPx(c)));
  }
}

QColor QColorButton::color() const
{
  return clr_;
}


///////////////////////////////////////////////////////////////////////////////////////////////


QMultiColorButton::QMultiColorButton(QWidget *p) : QToolButton(p)
{
  setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonIconOnly);
  setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);
  setIconSize(QSize(32, 16));
}

void QMultiColorButton::addItem(int id, const QString &name, const QColor &c)
{
  if (!menu()) {
    setMenu(new QMenu);
  }
  auto act = menu()->addAction(name);
  act->setData(id);
  act->setIcon(colorPx(c));
  colors_[id] = c;
  colorChanged();
  QObject::connect(act, &QAction::triggered, this, [this, act, id]() {
    auto t = QColorDialog::getColor(colors_[id], this, act->text());
    if (t.isValid()) {
      setItemColor(id, t);
    }
  });
}

void QMultiColorButton::setItemColor(int id, const QColor &c)
{
  if (auto a = actionFromId(id)) {
    a->setIcon(colorPx(c));
    colors_[id] = c;
    colorChanged();
  }
}

QColor QMultiColorButton::itemColor(int id) const
{
  return colors_[id];
}

QString QMultiColorButton::itemName(int id) const
{
  if (auto a = actionFromId(id)) {
    return a->text();
  }
  return QString();
}

QList<int> QMultiColorButton::items() const
{
  return colors_.keys();
}

void QMultiColorButton::reset()
{
  colors_.clear();
}

void QMultiColorButton::colorChanged()
{
  const auto n = items().size();
  QPixmap px(iconSize());
  const int w = px.width() / n;
  int x = px.rect().left();
  QPainter p(&px);
  p.setPen(QColor("black"));
  p.save();
  for (auto id: items()) {
    auto c = colors_[id];
    if (!c.isValid()) {
      p.setBrush(QColor("white"));
    } else {
      p.setBrush(c);
    }
    p.drawRect(QRect(x, px.rect().top(), w, px.height()));
    if (!c.isValid()) {
      p.drawText(px.rect(), Qt::AlignCenter, "A");
    }
    x += w;
  }
  p.restore();
  p.drawRect(px.rect().adjusted(0, 0, -1, -1));
  setIcon(QIcon(px));
}

QAction *QMultiColorButton::actionFromId(int id) const
{
  for (auto a : menu()->actions()) {
    if (a->data().toInt() == id) {
      return a;
    }
  }
  ASSERT(!"should not happen");
  return nullptr;
}


///////////////////////////////////////////////////////////////////////////////////////////////


QProgressBarEx::QProgressBarEx()
{
  QObject::connect(theApp().mainWindow(), &MainWindow::themeChanged, this, [this]() {
    updateStyle();       
    });
  updateStyle();
}

void QProgressBarEx::updateStyle()
{
  QPalette plt = theApp().palette();
  setStyleSheet(QString(
    "QProgressBar { border: 1px solid %1; text-align: center; } "
    "QProgressBar::chunk { background-color: %1; width: 10px; } "
  ).arg(plt.color(QPalette::ColorRole::Highlight).name()));
}


///////////////////////////////////////////////////////////////////////////////////////////////


TabCloseButton::TabCloseButton(QTabBar *tabbar, QWidget *p)
{
  ThemeIcon::setup(this, "xmark");
  setAutoRaise(true);
  QObject::connect(this, &QAbstractButton::clicked, [this, tabbar, p]() {
    for (int j = 0; j < tabbar->count(); j ++) {
      const auto btn = tabbar->tabButton(j, QTabBar::ButtonPosition::RightSide);
      if (btn == this || btn == p) {
        Q_EMIT tabbar->tabCloseRequested(j);
        break;
      }
    }
    });
}


///////////////////////////////////////////////////////////////////////////////////////////////



#endif // NO_GUI
