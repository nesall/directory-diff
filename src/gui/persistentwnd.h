#ifndef PERSISTENTWND_H
#define PERSISTENTWND_H

#ifndef NO_GUI

#include <QWidget>
#include <QEvent>
#include <QVariant>
#include <QSize>
#include <QPoint>
#include <QString>
#include <QMap>
#include <QSet>


class PersistentWnd
{
public:

  class Event : public QEvent {
    QSize size_;
    QSize oldSize_;
  public:
    static int staticType();
    Event(const QSize &size, const QSize &oldSize);
    const QSize &size() const { return size_; }
    const QSize &oldSize() const { return oldSize_; }
  };

  PersistentWnd();
  virtual ~PersistentWnd();

  void setWnd(QWidget *w);
  void setResizeOnly(bool f);
  bool resizeOnly() const { return resizeOnly_; }

  bool isAvailable() const { return availableTopLeft_ || availableSize_ || availableCustomKey_; }
  const QPoint topLeft() const { return topLeft_; }
  const QSize size() const { return size_; }

  void addNewKey(const QString &key);
  void setKeyValue(const QString &key, const QVariant &value);
  QVariant valueForKey(const QString &key) const;

private:
  void readSettings(QWidget *w);
  void writeSettings(QWidget *w);
  bool fetchData(const QString &);

private:
  struct Obj : public QObject
  {
    Obj(PersistentWnd &pw);
    virtual bool eventFilter(QObject *o, QEvent *e) override;
    PersistentWnd &pw_;
  } *obj_;
  friend struct Obj;
  QWidget *target_ = nullptr;
  bool availableTopLeft_ = false;
  bool availableSize_ = false;
  bool availableCustomKey_ = false;
  QPoint topLeft_;
  QSize size_;
  bool resizeOnly_ = false;
  QSet<QString> keys_;
  QMap<QString, QVariant> keyValues_;
};


#endif // NO_GUI
#endif // PERSISTENTWND_H
