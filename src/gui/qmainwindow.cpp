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
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QCheckBox>
#include <QCloseEvent>
#include <QPlainTextEdit>

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
  actStats_ = mainToolBar_->addAction(QIcon(":/info.svg"), tr("Stats"), this, [this] {
    if (auto *directory = currentDirDiff()) directory->showComparisonStats();
  });
  actStats_->setToolTip(tr("Show directory comparison statistics"));
  actStats_->setEnabled(false);
  auto *settingsAction = mainToolBar_->addAction(QIcon(":/cog"), tr("Settings"));
  settingsAction->setToolTip(tr("Edit directory comparison exclusions"));
  connect(settingsAction, &QAction::triggered, this, &QMainWindowDiff::showSettings);
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
      auto btn = new QCheckBox;
      btn->setCheckable(true);
      btn->setText(text);
      btn->setToolTip(tip);
      //btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
      //btn->setAutoRaise(true);
      if (clr.isValid()) {
        btn->setStyleSheet(QString("QAbstractButton { border-bottom: 2px solid %1; }").arg(clr.name()));
      }
      btn->setChecked(settings.value(text, true).toBool());
      btn->setFixedWidth(90);
      lay->addWidget(btn);
      lay->addStretch();
      lay->setContentsMargins(0, 1, 0, 1);
      lay->setSpacing(1);
      connect(btn, &QAbstractButton::toggled, this, [this, btn, text, handler](bool checked) {
        handler(checked);
        QSettings settings;
        settings.setValue(text, checked);
        });
      return btn;
    };

  QSettings settings;
  settings.beginGroup("MainWindow");

  auto btnIdentical = makeFilterToggle(tr("= Identical"), tr("Show identical files"), vlFilter0, QColor(0xF0, 0xF0, 0xFF),
    [this](bool on) { if (auto *d = currentDirDiff()) d->proxyModel()->setShowIdentical(on); });
  auto btnModified = makeFilterToggle(tr("\u2260 Modified"), tr("Show modified files"), vlFilter0, QColor(0xFF, 0xF0, 0xCC),
    [this](bool on) { if (auto *d = currentDirDiff()) d->proxyModel()->setShowModified(on); });
  auto btnRemoved = makeFilterToggle(tr("- Removed"), tr("Show removed files"), vlFilter1, QColor(0xFF, 0xCC, 0xCC),
    [this](bool on) { if (auto *d = currentDirDiff()) d->proxyModel()->setShowRemoved(on); });
  auto btnAdded = makeFilterToggle(tr("+ Added"), tr("Show added files"), vlFilter1, QColor(0xCC, 0xFF, 0xCC),
    [this](bool on) { if (auto *d = currentDirDiff()) d->proxyModel()->setShowAdded(on); });

  settings.endGroup();

  connect(actNewDirDiff_, &QAction::triggered, this, [this] {
    auto *w = new QDirDiffWidget();
    auto *tab = tabWidget();
    const int idx = tab->addTab(w, QIcon(":/folder"), tr("Directory Diff"));
    tab->setCurrentIndex(idx);
    connect(w, &QDirDiffWidget::statusMessage, this, [this, tab, w](const QString &msg) {
      if (tab->currentWidget() == w) statusBar()->showMessage(msg);
      });
    connect(w, &QDirDiffWidget::compareStarted, [this, tab, w] {
      const int index = tab->indexOf(w);
      const QString left = w->leftPath();
      tab->setTabText(index, left.length() > 20 ? QStringLiteral("...") + left.right(20) : left);
      tab->setTabToolTip(index, tr("Comparing %1 vs %2").arg(left, w->rightPath()));
      });
    connect(w, &QDirDiffWidget::comparisonStatsAvailabilityChanged, this, &QMainWindowDiff::updateStatsAction);
    connect(w, &QDirDiffWidget::compareFileRequested, this, &QMainWindowDiff::compareFileRequested);
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
    } else if (auto *file = qobject_cast<QFileDiffWidget *>(tabWidget()->currentWidget())) {
      file->compareFiles();
    }
    });

  connect(newMenu->menuAction(), &QAction::triggered, this, [this] { actNewDirDiff_->trigger(); });
}

void QMainWindowDiff::showSettings()
{
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Settings"));
  dialog.resize(500, 320);

  auto *layout = new QVBoxLayout(&dialog);
  auto *label = new QLabel(tr("Directory comparison &exclusions"), &dialog);
  auto *help = new QLabel(tr("Enter one pattern per line. Use * for wildcards and end folder patterns with /. Exclusions apply to both sides."), &dialog);
  help->setWordWrap(true);
  auto *editor = new QPlainTextEdit(QDirDiffWidget::exclusions().join('\n'), &dialog);
  editor->setPlaceholderText(QStringLiteral("*.tmp\nbuild/\nthird_party/cache/"));
  label->setBuddy(editor);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  layout->addWidget(label);
  layout->addWidget(help);
  layout->addWidget(editor, 1);
  layout->addWidget(buttons);
  editor->setFocus();

  if (dialog.exec() != QDialog::Accepted) return;

  QStringList patterns;
  for (const auto &line : editor->toPlainText().split('\n')) {
    const QString pattern = line.trimmed();
    if (!pattern.isEmpty())
      patterns.append(pattern);
  }
  patterns.removeDuplicates();
  QDirDiffWidget::setExclusions(patterns);
  statusBar()->showMessage(tr("Exclusions saved. Run Compare to apply them."));
}

void QMainWindowDiff::createCentralWidgets()
{
  auto *central = new QTabWidget(this);
  setCentralWidget(central);
  central->setTabsClosable(true);
  connect(central, &QTabWidget::currentChanged, this, [this, central] {
    const auto *directory = qobject_cast<QDirDiffWidget *>(central->currentWidget());
    const auto *file = qobject_cast<QFileDiffWidget *>(central->currentWidget());
    const QString status = directory ? directory->statusText() : file ? file->statusText() : QString();
    statusBar()->showMessage(status.isEmpty() ? tr("Ready") : status);
    updateStatsAction();
    });
  actNewDirDiff_->trigger();
  connect(central, &QTabWidget::tabCloseRequested, this, [central](int index) {
    auto *page = central->widget(index);
    if (!page) return;
    if (auto *file = qobject_cast<QFileDiffWidget *>(page)) {
      if (!file->confirmDiscardChanges()) return;
    }
    central->removeTab(index);
    page->deleteLater();
    });
}

void QMainWindowDiff::closeEvent(QCloseEvent *event)
{
  auto *tab = tabWidget();
  for (int i = 0; i < tab->count(); ++i) {
    if (auto *file = qobject_cast<QFileDiffWidget *>(tab->widget(i))) {
      if (!file->confirmDiscardChanges()) {
        event->ignore();
        return;
      }
    }
  }
  QMainWindow::closeEvent(event);
}

QTabWidget *QMainWindowDiff::tabWidget() const
{
  return static_cast<QTabWidget *>(centralWidget());
}

QDirDiffWidget *QMainWindowDiff::currentDirDiff() const
{
  return qobject_cast<QDirDiffWidget *>(tabWidget()->currentWidget());
}

void QMainWindowDiff::updateStatsAction()
{
  const auto *directory = currentDirDiff();
  actStats_->setEnabled(directory && directory->hasComparisonStats());
}

void QMainWindowDiff::compareFileRequested(const QString &leftFile, const QString &rightFile,
  bool leftMissing, bool rightMissing)
{
  auto w = new QFileDiffWidget();
  auto tab = tabWidget();
  const int idx = tab->addTab(w, QIcon(":/file"), tr("File Diff"));
  tab->setCurrentIndex(idx);
  connect(w, &QFileDiffWidget::statusMessage, this, [this, tab, w](const QString &msg) {
    if (tab->currentWidget() == w) statusBar()->showMessage(msg);
    });
  connect(w, &QFileDiffWidget::compareStarted, [this, tab, w] {
    const int index = tab->indexOf(w);
    const QString left = w->leftFile();
    tab->setTabText(index, left.length() > 20 ? QStringLiteral("...") + left.right(20) : left);
    tab->setTabToolTip(index, tr("Comparing %1 vs %2").arg(left, w->rightFile()));
    });
  w->setFiles(leftFile, rightFile, leftMissing, rightMissing);
  if (!leftFile.isEmpty() && !rightFile.isEmpty()) {
    w->compareFiles();
  }
}
