#include "qmainwindow.h"
#include "qdirdiff.h"
#include "qfilediff.h"
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QToolButton>
#include <QTabWidget>
#include <QSettings>
#include <QFileDialog>

QMainWindowDiff::QMainWindowDiff(QWidget *parent)
  : QMainWindow(parent)
{
  createToolBar();
  createCentralWidgets();
  statusBar()->showMessage(tr("Ready"));
}

void QMainWindowDiff::createToolBar()
{
  mainToolBar_ = addToolBar(tr("Main Toolbar"));
  mainToolBar_->setMovable(false);
  mainToolBar_->setIconSize(QSize(32, 24));
  mainToolBar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

  QMenu *newMenu = new QMenu(tr("New"), this);
  actNewDirDiff_ = newMenu->addAction(tr("Directory Diff"));
  actNewFileDiff_ = newMenu->addAction(tr("File Diff"));
  mainToolBar_->addAction(newMenu->menuAction());
  newMenu->menuAction()->setIcon(QIcon::fromTheme(":/folder"));
  actNewDirDiff_->setIcon(QIcon::fromTheme(":/folder"));
  actNewFileDiff_->setIcon(QIcon::fromTheme(":/file"));

  actCompare_ = mainToolBar_->addAction(tr("Compare"));
  actCompare_->setIcon(QIcon::fromTheme(":/refresh"));
  mainToolBar_->addSeparator();

  auto filterWidget0 = new QWidget(this);
  mainToolBar_->addWidget(filterWidget0);
  auto vlFilter0 = new QVBoxLayout(filterWidget0);

  auto filterWidget1 = new QWidget(this);
  mainToolBar_->addWidget(filterWidget1);
  auto vlFilter1 = new QVBoxLayout(filterWidget1);


  auto makeFilterToggle = [this](const QString &text, const QString &tip, QBoxLayout *lay, const QColor &clr, std::function<void(bool)> handler)
    {
      QSettings settings;
      auto btn = new QToolButton;
      btn->setCheckable(true);
      btn->setText(text);
      btn->setToolTip(tip);
      btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
      btn->setAutoRaise(true);
      if (clr.isValid())
        btn->setStyleSheet(QString("QToolButton { background: %1; }").arg(clr.name()));
      btn->setChecked(settings.value(text, true).toBool());
      btn->setFixedWidth(80);
      lay->addWidget(btn);
      lay->addStretch();
      lay->setContentsMargins(0, 1, 0, 1);
      lay->setSpacing(1);
      connect(btn, &QToolButton::toggled, this, [this, btn, text, handler](bool checked) {
        handler(checked);
        QSettings settings;
        settings.setValue(text, checked);
        });
      return btn;
    };

  QSettings settings;
  settings.beginGroup("MainWindow");

  auto btnIdentical = makeFilterToggle(tr("= Identical"), tr("Show identical files"), vlFilter0, {},
    [this](bool on) { currentDirDiff()->proxyModel()->setShowIdentical(on); });
  auto btnModified = makeFilterToggle(tr("\u2260 Modified"), tr("Show modified files"), vlFilter0, QColor(0xFF, 0xF0, 0xCC),
    [this](bool on) { currentDirDiff()->proxyModel()->setShowModified(on); });
  auto btnRemoved = makeFilterToggle(tr("- Removed"), tr("Show removed files"), vlFilter1, QColor(0xFF, 0xCC, 0xCC),
    [this](bool on) { currentDirDiff()->proxyModel()->setShowRemoved(on); });
  auto btnAdded = makeFilterToggle(tr("+ Added"), tr("Show added files"), vlFilter1, QColor(0xCC, 0xFF, 0xCC),
    [this](bool on) { currentDirDiff()->proxyModel()->setShowAdded(on); });

  settings.endGroup();

  connect(actNewDirDiff_, &QAction::triggered, this, [this] {
    auto *w = new QDirDiffWidget();
    auto *tab = tabWidget();
    const int idx = tab->addTab(w, tr("Directory Diff"));
    tab->setCurrentIndex(idx);
    connect(w, &QDirDiffWidget::statusMessage, [this](const QString &msg) { statusBar()->showMessage(msg); });
    connect(w, &QDirDiffWidget::compareStarted, [this, tab, w] {
      auto left = w->leftPath();
      QString str = tr("Dir diff: %1").arg(left.length() > 20 ? QStringLiteral("...") + left.right(20) : left);
      tab->setTabText(tab->currentIndex(), str);
      auto right = w->rightPath();
      tab->setTabToolTip(tab->currentIndex(), tr("Comparing %1 vs %2").arg(left).arg(right));
      });
    connect(w, &QDirDiffWidget::compareFinished, [this] {
      });
    connect(w, &QDirDiffWidget::compareFileRequested, this, [this](const QString &leftFile, const QString &rightFile) {
      compareFileRequested(leftFile, rightFile);
      });
    });

  connect(actNewFileDiff_, &QAction::triggered, this, [this] {
    compareFileRequested({}, {});
    });

  connect(actCompare_, &QAction::triggered, this, [this, btnIdentical, btnModified, btnAdded, btnRemoved] {
    if (auto *d = currentDirDiff()) {
      d->proxyModel()->setShowIdentical(btnIdentical->isChecked());
      d->proxyModel()->setShowModified(btnModified->isChecked());
      d->proxyModel()->setShowAdded(btnAdded->isChecked());
      d->proxyModel()->setShowRemoved(btnRemoved->isChecked());
      d->compareDirs();
    }
    });

  connect(newMenu->menuAction(), &QAction::triggered, this, [this] { actNewDirDiff_->trigger(); });
}

void QMainWindowDiff::createCentralWidgets()
{
  auto *central = new QTabWidget(this);
  setCentralWidget(central);
  central->setTabsClosable(true);
  actNewDirDiff_->trigger();
  connect(central, &QTabWidget::tabCloseRequested, central, &QTabWidget::removeTab);
}

QTabWidget *QMainWindowDiff::tabWidget() const
{
  return static_cast<QTabWidget *>(centralWidget());
}

QDirDiffWidget *QMainWindowDiff::currentDirDiff() const
{
  return qobject_cast<QDirDiffWidget *>(tabWidget()->currentWidget());
}

void QMainWindowDiff::compareFileRequested(const QString &leftFile, const QString &rightFile)
{
  auto w = new QFileDiffWidget();
  auto tab = tabWidget();
  const int idx = tab->addTab(w, tr("File Diff"));
  tab->setCurrentIndex(idx);
  connect(w, &QFileDiffWidget::statusMessage, [this](const QString &msg) { statusBar()->showMessage(msg); });
  connect(w, &QFileDiffWidget::compareStarted, [this, tab, w] {
    auto left = w->leftFile();
    QString str = tr("File diff: %1").arg(left.length() > 20 ? QStringLiteral("...") + left.right(20) : left);
    tab->setTabText(tab->currentIndex(), str);
    auto right = w->rightFile();
    tab->setTabToolTip(tab->currentIndex(), tr("Comparing %1 vs %2").arg(left).arg(right));
    });
  w->setFiles(leftFile, rightFile);
  if (!leftFile.isEmpty() && !rightFile.isEmpty()) {
    w->compareFiles();
  }
}