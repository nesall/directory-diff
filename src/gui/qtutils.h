#pragma once

#ifndef NO_GUI
#include <QLineEdit>
#include <QKeyEvent>
#include <QStandardItem>
#include <QTreeView>
#include <QDialog>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QSplitter>
#include <QAbstractItemModel>
#include <QAbstractTableModel>
#include <QToolButton>
#include <QProgressBar>
#include <QColor>
#include <QTabBar>
#endif // NO_GUI
#include <QTimer>
#include <QVector>
#include <QList>
#include <QDateTime>
#include <QPointer>
#include <QTimer>
#include <QThread>
#include <QSettings>

#include <limits>
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <chrono>


QString doubleToLabel(double v, int decimals = 1, const QString &suffix = {});
inline QString doubleToPercentLabel(double percent, int decimals = 1) { return doubleToLabel(percent, decimals, " %"); }

#ifndef NO_GUI
// rgb(r, g, b) => hex name
QColor rgbToColor(const QString &rgb);
QColor strToColor(const QString &s);
#endif

///////////////////////////////////////////////////////////////////////////////////////////////


class QSlotBase : public QObject {
  Q_OBJECT;

signals:
  void signal0();
  void signal1();
  void signal2();
  void signalInt0(int v);
  void signalInt1(int v);
  void signalBool0(bool v);
  void signalBool1(bool v);
  void signalIntInt(int a, int b);
  void signalIntStr(int a, const QString &str);
  void signalStr0(const QString &str);

public Q_SLOTS:
  virtual void slot0() {}
  virtual void slot1() {}
  virtual void slot2() {}
  void slotInt0(int ) {}
  virtual void slotStr0(const QString &) {}
};


///////////////////////////////////////////////////////////////////////////////////////////////

#ifndef NO_GUI
#if 1
class QInfoLineEdit : public QLineEdit
{
  Q_OBJECT
public:
  QInfoLineEdit(QWidget *p = 0);
  void setInfoText(const QString &info);
  QString infoText() const { return infoText_; }
  void escapeKeyInvalidatesText(bool f);
private:
  virtual void paintEvent(QPaintEvent *e);
  QString infoText_;
private slots:
  void escHooked();
private:
  bool escapeInvalidates_ = 0;
};
#endif

///////////////////////////////////////////////////////////////////////////////////////////////

class HookKey : public QObject {
  QWidget *w_;
  Qt::Key key_;
  Q_OBJECT
public:
  HookKey(QWidget *w, Qt::Key key) : QObject(w), w_(w), key_(key) {
    w_->installEventFilter(this);
  }
  Qt::Key key() const { return key_; }
private:
  virtual bool eventFilter(QObject *obj, QEvent *ev) {
    if (obj == w_) {
      if (QEvent::KeyPress == ev->type()) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(ev);
        if (ke->key() == key_) {
          emit hooked();
          return true;
        }
      }
    }
    return QObject::eventFilter(obj, ev);
  }
signals:
  void hooked();
};

///////////////////////////////////////////////////////////////////////////////////////////////

class QVertLine : public QFrame
{
public:
  QVertLine() {
    setLineWidth(1);
    setMidLineWidth(0);
    setFrameShape(QFrame::VLine);
    setFrameShadow(QFrame::Sunken);
  }
};

class QHorzLine : public QFrame
{
public:
  QHorzLine() {
    setLineWidth(1);
    setMidLineWidth(0);
    setFrameShape(QFrame::HLine);
    setFrameShadow(QFrame::Sunken);
  }
};
#endif // NO_GUI


///////////////////////////////////////////////////////////////////////////////////////////////

QString qString(const std::string &s);
QByteArray qByteArray(const std::string &s);

QDateTime dateStrToDateTime(const std::string &dateStr);

QDateTime dateStrToDateTime(const std::string &dateStr, const std::string &timeStr);

QString toSepString(double v, bool abbr = true);

std::vector<std::string> stdStringList(const QStringList &list);

std::map<std::string, std::string> stdMap(const std::map<QString, QString> &m);

template <class T>
QVector<T> stdVecToQVec(const std::vector<T> &v) {
  QVector<T> qv;
  for (const auto &t: v) {
    qv.append(t);
  }
  return qv;
}


///////////////////////////////////////////////////////////////////////////////////////////////


class MinMaxFinder {
  double min_ = (std::numeric_limits<double>::max)();
  double max_ = (std::numeric_limits<double>::lowest)();
  QDateTime minDate_;
  QDateTime maxDate_;

public:
  MinMaxFinder() {
    minDate_ = QDateTime::fromString("2050-01-01", "yyyy-MM-dd");
    maxDate_ = QDateTime::fromString("1980-01-01", "yyyy-MM-dd");
  }
  MinMaxFinder(const std::string &mind, const std::string &maxd, double min, double max) : min_(min), max_(max) {
    minDate_ = QDateTime::fromString(qString(mind), "yyyy-MM-dd");
    maxDate_ = QDateTime::fromString(qString(maxd), "yyyy-MM-dd");
  }

  void resetReals() {
    min_ = (std::numeric_limits<double>::max)();
    max_ = (std::numeric_limits<double>::lowest)();
  }

  void processReal(double low, double high) {
    if (low < min_) min_ = low;
    if (max_ < high) max_ = high;
  }

  void processReal(double r) {
    processReal(r, r);
  }

  void processDate(const std::string &dateStr) {
    auto date = dateStrToDateTime(dateStr);
    if (date < minDate_) minDate_ = date;
    if (maxDate_ < date) maxDate_ = date;
  }
  
  void processDate(const QDateTime &date) {
    if (date < minDate_) minDate_ = date;
    if (maxDate_ < date) maxDate_ = date;
  }

  double minReal() const {
    return min_;
  }

  double maxReal() const {
    return max_;
  }

  double rangeReal() const {
    return max_ - min_;
  }

  void inflateRealRangeSlightly() {
    min_ = minReal() - rangeReal() * 0.02 - std::numeric_limits<double>::epsilon();
    max_ = maxReal() + rangeReal() * 0.02 + std::numeric_limits<double>::epsilon();
  }

  void inflateRealBy(qreal up, qreal bottom) {
    min_ = minReal() - bottom;
    max_ = maxReal() + up;
  }

  const QDateTime &minDate() const {
    return minDate_;
  }
  const QDateTime &maxDate() const {
    return maxDate_;
  }
};


///////////////////////////////////////////////////////////////////////////////////////////////

/// @brief Not supposed to be used directly. See QDelayedCall.
class QDelayedMethodHelper : public QObject
{
  Q_OBJECT

protected:
  QDelayedMethodHelper(std::chrono::milliseconds ms, uint times) : timesMax_(times) {
    timer_ = new QTimer(this);
    QObject::connect(timer_, SIGNAL(timeout()), this, SLOT(onTimer()));
    timer_->start(ms);
  }

  virtual ~QDelayedMethodHelper() {}
protected:
  void called() {
    if (++timesSoFar_ == timesMax_) {
      if (!timer_.isNull()) {
        timer_->stop();
        deleteLater();
      }
    }
  }
  QTimer *timer() const { return timer_; }

private slots:
  virtual void onTimer() = 0;

private:
  QPointer<QTimer> timer_;
  uint timesSoFar_ = 0;
  uint timesMax_;
};

/// @brief Makes a delayed call to a method (with no args) or a function (with no args) in a sense of QTimer(0).
class QDelayedCall
{
public:
#if 0
  template <class TObject, class TMethod>
  static QTimer *launch(TObject *p, TMethod f, int milliseconds = 0, uint times = 1) {
    //      NULL_RETURN_ASSERT( p );
    return (new QDelayedMethod_<TObject, TMethod>(p, f, milliseconds, times))->timer();
  }
  template <class TFunction>
  static QTimer *launch(TFunction f, int milliseconds = 0, uint times = 1) {
    return (new QDelayedFunction_<TFunction>(f, milliseconds, times))->timer();
  }
#endif

  static QTimer *launch(std::function<void ()> f, std::chrono::milliseconds milliseconds = std::chrono::milliseconds(0), uint times = 1) {
    return (new QDelayedLambda_(f, milliseconds, times))->timer();
  }

private:
#if 0
  template <class TObject, class TMethod>
  class QDelayedMethod_ : protected QDelayedMethodHelper
  {
  public:
    QDelayedMethod_(TObject *p, TMethod f, int ms, uint times) : QDelayedMethodHelper(ms, times), pObj_(p), func_(f) {}
    QTimer *timer() const { return QDelayedMethodHelper::timer(); }
  private:
    void onTimer() {
      if (pObj_) {
        (pObj_->*(func_))();
      }
      called();
    }
    TObject *pObj_;
    TMethod func_;
  };

  template <class TFunction>
  class QDelayedFunction_ : protected QDelayedMethodHelper
  {
  public:
    QDelayedFunction_(TFunction f, int ms, uint times) : QDelayedMethodHelper(ms, times), func_(f) {}
    QTimer *timer() const { return QDelayedMethodHelper::timer(); }
  private:
    void onTimer() {
      (*func_)();
      called();
    }
    TFunction func_;
  };
#endif

  class QDelayedLambda_ : protected QDelayedMethodHelper
  {
  public:
    QDelayedLambda_(std::function<void ()> f, std::chrono::milliseconds ms, uint times) : QDelayedMethodHelper(ms, times), func_(f) {}
    QTimer *timer() const { return QDelayedMethodHelper::timer(); }
  private:
    std::function<void ()> func_;
    void onTimer() override {
      func_();
      called();
    }
  };
};



///////////////////////////////////////////////////////////////////////////////////////////////

#ifndef NO_GUI
class QListFilterWidget : public QWidget
{
  typedef QWidget Super;
private:
  Q_OBJECT;
public:
  QListFilterWidget(QWidget *p = 0);
  QAbstractItemView *view() const { return list_; }
  void setColumnTitle(const QString &s) { columnTitle_ = s; }
  QString columnTitle() const { return columnTitle_; }
  void setListItems(const QStringList &vec);
  void addItem(const QString &item, const QVariant &data = QVariant());
  void addItem(QStandardItem *item);
  QStandardItem *takeItem(int row);
  QString itemAt(int row, QVariant *data = 0) const;
  QString selectedItem() const;
  QVariant selectedData() const { return selData_; }
  void setSelectedItem(int j);
  void setFilterHint(const QString &hint);
  QString filterText() const { return filter_->text(); }
  int count() const { Q_ASSERT(view() && view()->model()); return view()->model()->rowCount(); }
  void clear();
  void setItemsDeletable(bool f);
  bool itemsDeletable() const { return itemsDeletable_; }

private slots:
  void filterChanged(const QString &);

signals:
  void selectionChanged(const QString &);
  void doubleClicked(const QModelIndex &);
  void itemDeleteRequested(int row);

private:
  QLineEdit *filter_;
  QTreeView *list_;
  QLineEdit *selection_;
  QString columnTitle_;
  QVariant selData_;
  bool itemsDeletable_ = false;
};

///////////////////////////////////////////////////////////////////////////////////////////////

enum class ColorMode { Normal, Red, Red1, Green };

class IconRef {
  QPixmap px_;
  QString name_;
  int w_;
  int h_;
  ColorMode mode_;
  friend class ThemeIcon;
public:
  IconRef(const char *name, int w = 0, int h = 0, ColorMode mode = ColorMode::Normal);
  IconRef(const QString &name, int w = 0, int h = 0, ColorMode mode = ColorMode::Normal);
  IconRef(const QPixmap &icon, int w = 0, int h = 0, ColorMode mode = ColorMode::Normal);
  bool isNull() const;
  void apply(QObject *target) const;
};

class ThemeIcon : public QObject {
  Q_OBJECT;
private:
  IconRef iconRef_;
  QPointer<QObject> target_;
private:
  ThemeIcon(QObject *target, const IconRef &iref);
  static bool isDarkTheme();
  static QString themedPostfix();
  void updateIcon();
  static QPixmap fromName_p(const QString &name, ColorMode mode);
public:
  static QPixmap fromIconRef(const IconRef &iref);
  static QPixmap fromName(const QString &name, int w = 0, int h = 0, const QPixmap &pxAlt = QPixmap(), ColorMode mode = ColorMode::Normal);
  static QPixmap fromName(const QString &name, const QPixmap &pxAlt, ColorMode mode = ColorMode::Normal) { return fromName(name, 0, 0, pxAlt, mode); }
  static void setup(QObject *target, const IconRef &iref);
};

double isDarkColor(const QColor &clr);

double perceivedBrightness(const QColor &clr);

QColor contrasting(const QColor &clr, double amount);

QColor overlaidWith(const QColor &dest, const QColor &src);

QColor colorForMode(ColorMode mode, const QColor &def = {});

///////////////////////////////////////////////////////////////////////////////////////////////

void addActionToWidget(QWidget *owner, QAction *&act, const QString &title, const QIcon &px = QIcon(), const QKeySequence &keyseq = QKeySequence());
void addActionToWidget(QWidget *owner, QAction *&act, const QString &title, const IconRef &iid, const QKeySequence &keyseq = QKeySequence());

QToolButton *createToolbutton(QAction *action);
QToolButton *createToolbutton(const QString &title, const QIcon &icon = QIcon(), const QString &tooltip = {});
QToolButton *createToolbutton(const QString &title, const IconRef &iid, const QString &tooltip = {});

void restoreSplitterSizes(QSplitter *splitter, const QString &key);
void setToSaveSplitterSizes(QSplitter *splitter, const QString &key);

QString msToLabel(long ms);

template <class T>
T* findAncestor(QWidget *w)
{
  while (w) {
    if (T *t = qobject_cast<T *>(w)) {
      return t;
    }
    w = w->parentWidget();
  }
  return 0;
}

// Does not require Q_OBJECT macro.
template<typename T>
  requires std::is_pointer_v<T>
QList<T> findChildrenDynamic(const QObject *parent, const QString &name)
{
  QList<T> result;
  if (!parent)
    return result;
  const QList<QObject *> &children = parent->children();
  for (QObject *child : children) {
    if (T t = dynamic_cast<T>(child)) {
      if (name.isEmpty() || child->objectName() == name) 
        result.append(t);      
    }
    result.append(findChildrenDynamic<T>(child, name));
  }
  return result;
}



///////////////////////////////////////////////////////////////////////////////////////////////

class QListInputDialog : public QDialog {
  QPlainTextEdit *edit_;
public:
  QListInputDialog(QWidget *p = nullptr);

  QStringList getList() const;
};


///////////////////////////////////////////////////////////////////////////////////////////////


class QBaseComboBox : public QComboBox
{
  typedef QComboBox Super;

private:
  Q_OBJECT;

public:
  QBaseComboBox(QWidget *parent = 0);
  void showPopupOnFocus(bool on);
  bool showPopupOnFocus() const { return popupOnFocus_; }

protected:
  virtual void focusInEvent(QFocusEvent *e);
  virtual bool eventFilter(QObject *obj, QEvent *e);
  virtual void showPopup();

signals:
  void popupAboutToBeShown();
  void popupAboutToBeHidden();
  void popupRequested();

private:
  bool popupOnFocus_ = false;
};


///////////////////////////////////////////////////////////////////////////////////////////////


class QErrorReportLineEdit : public QLineEdit
{
  typedef QLineEdit Super;

public:
  QErrorReportLineEdit(QWidget *p = 0) : Super(p), colorError_(Qt::red), error_(false) {}
  void setErrorColor(const QColor &clr) { colorError_ = clr; }
  QColor errorColor() const { return colorError_; }
  void setErrorMode(bool error) { error_ = error; update(); }
  bool errorMode() { return error_; }

protected:
  virtual void paintEvent(QPaintEvent *e);

private:
  QColor colorError_;
  bool error_;
};


///////////////////////////////////////////////////////////////////////////////////////////////

void registerAsyncQThread(QThread *p);

template <typename R>
class QAsyncResult : public QSlotBase {
  QThread thread_;
  R result_;
public:
  template <typename F>
  QAsyncResult(F func, QObject *p = nullptr) {
    setParent(p);
    registerAsyncQThread(&thread_);
    moveToThread(&thread_);
    QObject::connect(&thread_, &QThread::started, this, [this, func]() {
      result_ = func();
      thread_.quit();
      emit signal0();
    });
    thread_.start();
  }
  ~QAsyncResult() { thread_.wait(); }
  R result() const { return result_; }
};


///////////////////////////////////////////////////////////////////////////////////////////////


class QDelayedTask : public QObject {
  Q_OBJECT;
private:
  struct Task {
    QTimer *timer_;
    std::function<void()> func_;
    Task(QTimer *t = nullptr, std::function<void()> f = nullptr) : timer_(t), func_(f) {}
  };
  std::map<int, Task> tasks_;
public:
  QDelayedTask(QObject *p = nullptr);
  ~QDelayedTask();
  void addTask(int id, std::function<void()> f, int msDelay = 0);
  void cancelTask(int id);
  void commitTaskNow(int id);
private slots:
  void onTimer();
};


///////////////////////////////////////////////////////////////////////////////////////////////


class QAccordionWidget : public QWidget {
  Q_OBJECT;
private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
public:
  QAccordionWidget(QWidget *p = nullptr);
  void addSection(const QString &title, const QPixmap &icon = QPixmap());
  void setContentWidget(int j, QWidget *contentWidget);
  int sectionCount() const;
  void wrapUp();
  void setExpandedSection(int j);
signals:
  void sectionExpanded(int j);
private:
  void addSection(QWidget *header);
};


///////////////////////////////////////////////////////////////////////////////////////////////

template <class BaseModelType>
class QAbstractItemModelBase : public BaseModelType {
protected:
  QStringList headers_;
public:
  QAbstractItemModelBase() {
  }
private:
  virtual QVariant dataDisplay(const QModelIndex &) const { return QVariant(); }
  virtual QVariant colorDisplay(const QModelIndex &) const { return QVariant(); }
  virtual QVariant backgroundDisplay(const QModelIndex &) const { return QVariant(); }
  virtual QVariant fontDisplay(const QModelIndex &) const { return QVariant(); }
  virtual QVariant textAlignDisplay(const QModelIndex &) const { return QVariant(); }
  QVariant data(const QModelIndex &index, int role) const override {
    if (!index.isValid()) return QVariant();
    QVariant v;
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
      v = dataDisplay(index);
      break;
    case Qt::ForegroundRole:
      v = colorDisplay(index);
      break;
    case Qt::BackgroundRole:
      v = backgroundDisplay(index);
      break;
    case Qt::DecorationRole:
      break;
    case Qt::FontRole:
      v = fontDisplay(index);
      break;
    case Qt::TextAlignmentRole:
      v = textAlignDisplay(index);
      break;
    }
    return v;
  }
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override {
    if (role == Qt::DisplayRole) {
      if (orientation == Qt::Horizontal)
        return headers_[section];
      else
        return section + 1;
    }
    return QVariant();
  }
  int columnCount(const QModelIndex & = QModelIndex()) const override {
    return headers_.size();
  }
};

using QAbstractItemModelEx = QAbstractItemModelBase<QAbstractItemModel>;
using QAbstractTableModelEx = QAbstractItemModelBase<QAbstractTableModel>;


///////////////////////////////////////////////////////////////////////////////////////////////


class QColorButton : public QToolButton {
  Q_OBJECT;
  QColor clr_;
public:
  QColorButton(const QColor &c = QColor("white"), QWidget *p = nullptr);
  void setColor(const QColor &c);
  QColor color() const;
signals:
  void colorChanged();
};


///////////////////////////////////////////////////////////////////////////////////////////////


class QMultiColorButton : public QToolButton {
  Q_OBJECT;
private:
  QMap<int, QColor> colors_;
public:
  QMultiColorButton(QWidget *p = nullptr);
  void addItem(int id, const QString &name, const QColor &c = {});
  void setItemColor(int id, const QColor &c);
  QColor itemColor(int id) const;
  QString itemName(int id) const;
  QList<int> items() const;
  void reset();
private:
  void colorChanged();
  QAction *actionFromId(int id) const;
};


///////////////////////////////////////////////////////////////////////////////////////////////

class QProgressBarEx : public QProgressBar {
  void updateStyle();
public:
  QProgressBarEx();
};


///////////////////////////////////////////////////////////////////////////////////////////////

class TabCloseButton : public QToolButton {
public:
  TabCloseButton(QTabBar *tabbar, QWidget *p = nullptr);
};


#endif // NO_GUI


///////////////////////////////////////////////////////////////////////////////////////////////
