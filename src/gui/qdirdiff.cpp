#include "qdirdiff.h"
#include "qfilediff.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QScrollBar>
#include <QSettings>
#include <QFileInfo>
#include <QColor>
#include <QPainter>
#include <QFont>
#include <QBrush>
#include <QMenu>
#include <QShortcut>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QStyle>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QMessageBox>
#include <algorithm>

#define DIRDIFF_HISTORY_SIZE 20
#define DIRDIFF_HISTORY_KEY  "DirDiffHistory"
#define FILEDIFF_HISTORY_KEY  "FileDiffHistory"

namespace {
  constexpr auto exclusionsSettingsKey = "DirectoryDiff/exclusions";
}

// ─────────────────────────────────────────────────────────────────────────────

QGenericPanelHeader::QGenericPanelHeader(const QString &side, bool dirPanel, QWidget *parent) 
  : QWidget(parent), dirPanel_(dirPanel)
{
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  auto *label = new QLabel(side, this);
  label->setMinimumWidth(32);
  pathCombo_ = new QComboBox(this);
  pathCombo_->setEditable(true);
  pathCombo_->setInsertPolicy(QComboBox::NoInsert);
  pathCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  browseBtn_ = new QToolButton(this);
  browseBtn_->setText("...");

  layout->addWidget(label);
  layout->addWidget(pathCombo_);
  layout->addWidget(browseBtn_);

  loadHistory();

  connect(browseBtn_, &QToolButton::clicked, this, [this, dirPanel] {
    if (dirPanel) {
      const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Directory"), pathCombo_->currentText());
      if (!dir.isEmpty()) {
        setPath(dir);
        emit pathChanged(dir);
      }
    } else {
      const QString file = QFileDialog::getOpenFileName(
        this, tr("Select File"), pathCombo_->currentText());
      if (!file.isEmpty()) {
        setPath(file);
        emit pathChanged(file);
      }
    }
    });

  connect(pathCombo_, &QComboBox::currentTextChanged, this, [this](const QString &t) {
    emit pathChanged(t);
    });

  connect(pathCombo_->lineEdit(), &QLineEdit::returnPressed, this, [this] {
    const QString path = pathCombo_->currentText();
    if (!path.isEmpty()) {
      saveHistory(path);
      loadHistory();
      emit returnPressed();
    }
    });
}

QString QGenericPanelHeader::currentPath() const
{
  return pathCombo_->currentText();
}

void QGenericPanelHeader::setPath(const QString &path)
{
  QSignalBlocker blocker(pathCombo_);
  saveHistory(path);
  loadHistory();
  pathCombo_->setCurrentText(path);
}

void QGenericPanelHeader::saveHistory(const QString &path)
{
  const QString entry = dirPanel_ ? path.trimmed() : path;
  if (entry.isEmpty() || (dirPanel_ && !QFileInfo(entry).isDir())) return;
  QSettings s;
  QStringList h = s.value(dirPanel_ ? DIRDIFF_HISTORY_KEY : FILEDIFF_HISTORY_KEY).toStringList();
  h.removeAll(entry);
  h.append(entry);
  while (h.size() > DIRDIFF_HISTORY_SIZE)
    h.removeFirst();
  s.setValue(dirPanel_ ? DIRDIFF_HISTORY_KEY : FILEDIFF_HISTORY_KEY, h);
}

void QGenericPanelHeader::loadHistory()
{
  QSignalBlocker blocker(pathCombo_);
  const QString current = pathCombo_->currentText();
  pathCombo_->clear();
  QSettings s;
  const QStringList h = s.value(dirPanel_ ? DIRDIFF_HISTORY_KEY : FILEDIFF_HISTORY_KEY).toStringList();
  for (const QString &p : h) {
    if (!dirPanel_ || (!p.trimmed().isEmpty() && QFileInfo(p.trimmed()).isDir()))
      pathCombo_->addItem(p);
  }
  if (!current.isEmpty())
    pathCombo_->setCurrentText(current);
}


// ─────────────────────────────────────────────────────────────────────────────

namespace {
  QColor bgForDiff(DiffType t) {
    switch (t) {
    case DiffType::Added:     return QColor(0xCC, 0xFF, 0xCC);   // green tint
    case DiffType::Removed:   return QColor(0xFF, 0xCC, 0xCC);   // red tint
    case DiffType::Modified:  return {};//QColor(0xFF, 0xF0, 0xCC);   // amber tint
    default:                  return {};
    }
  }
  QColor fgForDiff(DiffType t) {
    switch (t) {
    case DiffType::Added:     return QColor(0x00, 0x66, 0x00);
    case DiffType::Removed:   return QColor(0x99, 0x00, 0x00);
    case DiffType::Modified:  return {};//QColor(0x88, 0x55, 0x00);
    default:                  return {};
    }
  }
  QString markForDiff(DiffType t) {
    switch (t) {
    case DiffType::Added:    return QStringLiteral("+");   // exists only in B
    case DiffType::Removed:  return QStringLiteral("-");   // exists only in A
    case DiffType::Modified: return QStringLiteral("\u2260");
    default:                 return QStringLiteral("=");
    }
  }
  QString diffType2Str(DiffType t) {
    switch (t) {
    case DiffType::Added:    return QStringLiteral("Added");
    case DiffType::Removed:  return QStringLiteral("Removed");
    case DiffType::Modified: return QStringLiteral("Modified");
    default:                 return QStringLiteral("Identical");
    }
  }
}


// ─────────────────────────────────────────────────────────────────────────────

namespace {
  class QDirDiffTreeView : public QTreeView {
  public:
    QDirDiffTreeView(DiffColumn nameColumn, QWidget *parent)
      : QTreeView(parent), nameColumn_(static_cast<int>(nameColumn)) {}

  protected:
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override {
      if (index.siblingAtColumn(nameColumn_).data().isValid())
        QTreeView::drawBranches(painter, rect, index);
    }

  private:
    int nameColumn_;
  };

  class QCompactDelegate : public QStyledItemDelegate {
  public:
    QCompactDelegate(QWidget *parent) : QStyledItemDelegate(parent), owner_(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
      QSize size = QStyledItemDelegate::sizeHint(option, index);
      // QTreeView uses its first row to measure every row in uniform-height mode.
      const int iconHeight = owner_->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, owner_);
      size.setHeight(qMax(owner_->fontMetrics().height(), iconHeight) + 2);
      return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
      if (!index.data().isValid()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
      }

      QStyleOptionViewItem opt = option;
      initStyleOption(&opt, index);

      const auto *proxy = qobject_cast<const QSortFilterProxyModel *>(index.model());
      const auto *m = proxy
        ? static_cast<const QDirDiffModel *>(proxy->sourceModel())
        : static_cast<const QDirDiffModel *>(index.model());
      const QModelIndex sourceIndex = proxy ? proxy->mapToSource(index) : index;

      if (opt.state & QStyle::State_Selected) {
        if (QWidget *p = qobject_cast<QWidget *>(parent())) {
          opt.palette.setColor(QPalette::Text, p->palette().color(QPalette::HighlightedText));
          painter->fillRect(opt.rect, opt.palette.color(QPalette::Highlight));
        }
      } else {
        if (auto node = m->nodeForIndex(sourceIndex)) {
          auto diffType = node->diffType;
          if (QColor fg = fgForDiff(diffType); fg.isValid()) {
            opt.palette.setColor(QPalette::Text, fg);
          }
          if (QColor bg = bgForDiff(diffType); bg.isValid()) {
            painter->fillRect(opt.rect, bg);
          }
        }
      }
      QStyledItemDelegate::paint(painter, opt, index);
    }

  private:
    QWidget *owner_;
  };
} // anonymous namespace

QDirDiffWidget::QDirDiffWidget(QWidget *parent)
  : QWidget(parent)
  , model_(new QDirDiffModel(this))
{
  headerLeft_ = new QGenericPanelHeader(tr("Left:"), true, this);
  headerRight_ = new QGenericPanelHeader(tr("Right:"), true, this);

  auto *headerBar = new QWidget(this);
  auto *headerLayout = new QHBoxLayout(headerBar);
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->addWidget(headerLeft_, 1);
  headerLayout->addWidget(headerRight_, 1);

  proxy_ = new QDirDiffProxyModel(this);
  proxy_->setSourceModel(model_);

  viewLeft_ = new QDirDiffTreeView(DiffColumn::LeftName, this);
  viewRight_ = new QDirDiffTreeView(DiffColumn::RightName, this);
  setupViews();

  splitter_ = new QSplitter(Qt::Horizontal, this);
  splitter_->setChildrenCollapsible(false);
  splitter_->setHandleWidth(2);
  splitter_->addWidget(viewLeft_);
  splitter_->addWidget(viewRight_);
  splitter_->setStretchFactor(0, 1);
  splitter_->setStretchFactor(1, 1);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);
  layout->addWidget(headerBar);
  layout->addWidget(splitter_, 1);

  watcher_ = new QFutureWatcher<ComparisonResult>(this);
  connect(watcher_, &QFutureWatcher<ComparisonResult>::finished, this, &QDirDiffWidget::onDiffFinished);
  connect(headerLeft_, &QGenericPanelHeader::returnPressed, this, [this] { compareDirs(); });
  connect(headerRight_, &QGenericPanelHeader::returnPressed, this, [this] { compareDirs(); });
}

void QDirDiffWidget::setupViews()
{
  viewLeft_->setModel(proxy_);
  viewRight_->setModel(proxy_);

  viewLeft_->setColumnHidden(static_cast<int>(DiffColumn::RightName), true);
  viewLeft_->setColumnHidden(static_cast<int>(DiffColumn::DiffMark), true);

  viewRight_->setColumnHidden(static_cast<int>(DiffColumn::LeftName), true);

  auto delegate = new QCompactDelegate(this);

  for (QTreeView *v : { viewLeft_, viewRight_ }) {
    v->setItemDelegate(delegate);
    v->setUniformRowHeights(true);
    // Reserve the same viewport height even if only one pane needs horizontal scrolling.
    v->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    v->setIndentation(12);
    v->setStyleSheet("QTreeView::item { padding-top: 0px; padding-bottom: 0px; }");
    v->setAnimated(false);
    v->setItemsExpandable(true);
    v->setSortingEnabled(false);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->header()->setStretchLastSection(true);
    v->header()->setSectionResizeMode(QHeaderView::Interactive);
  }

  viewLeft_->header()->resizeSection(static_cast<int>(DiffColumn::DiffMark), 28);
  viewRight_->header()->resizeSection(static_cast<int>(DiffColumn::DiffMark), 28);

  connect(viewLeft_->verticalScrollBar(), &QScrollBar::valueChanged, viewRight_->verticalScrollBar(), &QScrollBar::setValue);
  connect(viewRight_->verticalScrollBar(), &QScrollBar::valueChanged, viewLeft_->verticalScrollBar(), &QScrollBar::setValue);

  connect(viewLeft_, &QTreeView::expanded, this, [this](const QModelIndex &idx) { syncExpansion(viewLeft_, viewRight_, idx, true); });
  connect(viewLeft_, &QTreeView::collapsed, this, [this](const QModelIndex &idx) { syncExpansion(viewLeft_, viewRight_, idx, false); });
  connect(viewRight_, &QTreeView::expanded, this, [this](const QModelIndex &idx) { syncExpansion(viewRight_, viewLeft_, idx, true); });
  connect(viewRight_, &QTreeView::collapsed, this, [this](const QModelIndex &idx) { syncExpansion(viewRight_, viewLeft_, idx, false); });

  auto connectSyncSelection = [this](QTreeView *source, QTreeView *target, DiffColumn targetColumn)
    {
      connect(source->selectionModel(), &QItemSelectionModel::currentChanged, this, [this, target, targetColumn]
      (const QModelIndex &current, const QModelIndex &)
        {
          if (syncingSelection_) return;
          syncingSelection_ = true;
          const QModelIndex counterpart = current.siblingAtColumn(static_cast<int>(targetColumn));
          target->selectionModel()->setCurrentIndex(counterpart,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
          target->scrollTo(counterpart);
          syncingSelection_ = false;
        }
      );
    };
  connectSyncSelection(viewLeft_, viewRight_, DiffColumn::RightName);
  connectSyncSelection(viewRight_, viewLeft_, DiffColumn::LeftName);

  for (QTreeView *v : { viewLeft_, viewRight_ }) {
    v->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(v, &QWidget::customContextMenuRequested, this, [this, v](const QPoint &pos)
      {
        if (comparing_) return;
        const QModelIndex idx = v->indexAt(pos);
        if (!idx.isValid() || !idx.data().isValid()) return;

        auto selectedIndexes = v->selectionModel()->selectedRows();
        if (!v->selectionModel()->isSelected(idx))
          selectedIndexes = { idx };

        QMenu menu;
        auto *actView = menu.addAction(tr("Compare files"));
        menu.addSeparator();
        auto *actLR = menu.addAction(tr("Copy Left → Right\tCtrl+W"));
        auto *actRL = menu.addAction(tr("Copy Right → Left\tCtrl+Q"));
        menu.addSeparator();
        static QIcon iconDel; 
        if (iconDel.isNull())
          iconDel = QApplication::style()->standardIcon(QStyle::SP_TrashIcon);
        auto *actDel = menu.addAction(tr("Delete"));

        DirDiffNode *node = model_->nodeForIndex(proxy_->mapToSource(idx));
        const bool existsOnSide = node->kind(v == viewLeft_) != DiffEntryKind::Missing;
        actView->setEnabled(!node->isDir());
        const auto hasSource = [&](bool left) {
          return std::any_of(selectedIndexes.begin(), selectedIndexes.end(), [&](const QModelIndex &selected) {
            return model_->nodeForIndex(proxy_->mapToSource(selected))->kind(left) != DiffEntryKind::Missing;
          });
        };
        actLR->setEnabled(hasSource(true));
        actRL->setEnabled(hasSource(false));
        actDel->setEnabled(existsOnSide && selectedIndexes.size() == 1);
        connect(actLR, &QAction::triggered, this, [this, selectedIndexes] { copySelection(selectedIndexes, true); });
        connect(actRL, &QAction::triggered, this, [this, selectedIndexes] { copySelection(selectedIndexes, false); });
        connect(actDel, &QAction::triggered, this, [this, idx, v] { deleteEntry(idx, v == viewLeft_); });
        connect(actView, &QAction::triggered, this, [this, idx] {
          DirDiffNode *node = model_->nodeForIndex(proxy_->mapToSource(idx));
          if (comparing_ || !node || node->isDir()) {
            return;
          }
          const QString rel = relPathForNode(node);
          //auto *dialog = new QFileDiffDialog(this);
          //dialog->setAttribute(Qt::WA_DeleteOnClose);
          //dialog->showDiff(leftPath() + '/' + rel, rightPath() + '/' + rel);
          emit compareFileRequested(QDir(model_->leftRoot()).filePath(rel), QDir(model_->rightRoot()).filePath(rel),
            node->leftKind == DiffEntryKind::Missing, node->rightKind == DiffEntryKind::Missing);
          });

        menu.exec(v->viewport()->mapToGlobal(pos));
      });
  }

  auto *scLR = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
  auto *scRL = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this);
  connect(scLR, &QShortcut::activated, this, [this]
    {
      copySelection(operationSelection(viewLeft_), true);
    }
  );
  connect(scRL, &QShortcut::activated, this, [this]
    {
      copySelection(operationSelection(viewRight_), false);
    }
  );
}

void QDirDiffWidget::syncExpansion(QTreeView * /*source*/, QTreeView *target, const QModelIndex &idx, bool expanded)
{
  if (syncingExpansion_) return;
  syncingExpansion_ = true;
  // idx is from the shared model, so it is valid for both views
  if (expanded)
    target->expand(idx);
  else
    target->collapse(idx);
  syncingExpansion_ = false;
}

QString QDirDiffWidget::relPathForNode(const DirDiffNode *node) const
{
  QStringList parts;
  for (auto *n = node; n && n->parent; n = n->parent)
    parts.prepend(n->name);
  return parts.join('/');
}

namespace {
  struct OperationReport {
    int files = 0;
    int folders = 0;
    int failed = 0;
    QString firstError;

    void fail(const QString &path, const QString &message)
    {
      if (failed++ == 0)
        firstError = QObject::tr("%1 (%2)").arg(path, message);
    }
  };

  bool isLink(const QFileInfo &info)
  {
    return info.isSymbolicLink() || info.isJunction();
  }

  bool matchesKind(const QFileInfo &info, DiffEntryKind kind)
  {
    return kind == DiffEntryKind::Directory ? info.isDir() : info.isFile();
  }

  QString operationPath(const QString &root, const QString &relative, OperationReport &report)
  {
    const QFileInfo rootInfo(root);
    QString path = rootInfo.isDir() ? rootInfo.canonicalFilePath() : QString();
    if (path.isEmpty()) {
      report.fail(root, QObject::tr("Comparison root is unavailable."));
      return {};
    }
    if (QDir::isAbsolutePath(relative)) {
      report.fail(relative, QObject::tr("Expected a path inside the comparison root."));
      return {};
    }

    const auto parts = relative.split('/');
    for (qsizetype i = 0; i < parts.size(); ++i) {
      const auto &part = parts[i];
      if (part.isEmpty() || part == "." || part == "..") {
        report.fail(relative, QObject::tr("Expected a path inside the comparison root."));
        return {};
      }
      path = QDir(path).filePath(part);
      const QFileInfo info(path);
      if (isLink(info)) {
        report.fail(path, QObject::tr("Symbolic links and junctions are skipped."));
        return {};
      }
      if (i + 1 < parts.size() && info.exists() && !info.isDir()) {
        report.fail(path, QObject::tr("A parent path is a file."));
        return {};
      }
    }
    return path;
  }

#ifdef Q_OS_WIN
  constexpr auto pathCaseSensitivity = Qt::CaseInsensitive;
#else
  constexpr auto pathCaseSensitivity = Qt::CaseSensitive;
#endif

  bool sameOrUnder(const QString &path, const QString &parent)
  {
    return path.compare(parent, pathCaseSensitivity) == 0 ||
      path.startsWith(parent.endsWith('/') ? parent : parent + '/', pathCaseSensitivity);
  }

  template<typename Visit>
  void visitChildren(const QFileInfo &directory, OperationReport &report, Visit visit)
  {
    std::error_code error;
    std::filesystem::directory_iterator entry(directory.filesystemFilePath(), error), end;
    for (; !error && entry != end; entry.increment(error))
      visit(QFileInfo(entry->path()));
    if (error)
      report.fail(directory.filePath(), QString::fromLocal8Bit(error.message().c_str()));
  }

  void copyEntry(const QFileInfo &source, const QFileInfo &destination, OperationReport &report)
  {
    const QString src = source.filePath();
    const QString dst = destination.filePath();
    if (isLink(source) || isLink(destination)) {
      report.fail(isLink(source) ? src : dst, QObject::tr("Symbolic links and junctions are skipped."));
      return;
    }
    if (source.isDir()) {
      if (destination.exists() && !destination.isDir()) {
        report.fail(dst, QObject::tr("A file occupies the destination folder."));
        return;
      }
      if (!destination.exists()) {
        if (!QDir().mkpath(dst)) {
          report.fail(dst, QObject::tr("Could not create the folder."));
          return;
        }
        ++report.folders;
      }
      visitChildren(source, report, [&](const QFileInfo &child) {
        copyEntry(child, QFileInfo(QDir(dst).filePath(child.fileName())), report);
      });
      return;
    }

    if (!source.isFile()) {
      report.fail(src, QObject::tr("Source file is unavailable."));
      return;
    }
    if (destination.exists() && !destination.isFile()) {
      report.fail(dst, QObject::tr("A folder or special entry occupies the destination file."));
      return;
    }

    QFile input(src);
    if (!input.open(QIODevice::ReadOnly)) {
      report.fail(src, input.errorString());
      return;
    }
    if (!QDir().mkpath(destination.absolutePath())) {
      report.fail(dst, QObject::tr("Could not create the parent folder."));
      return;
    }
    QSaveFile output(dst);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
      report.fail(dst, output.errorString());
      return;
    }
    QByteArray buffer(64 * 1024, Qt::Uninitialized);
    for (;;) {
      const qint64 size = input.read(buffer.data(), buffer.size());
      if (size < 0) {
        report.fail(src, input.errorString());
        return;
      }
      if (size == 0) break;
      if (output.write(buffer.data(), size) != size) {
        report.fail(dst, output.errorString());
        return;
      }
    }
    if (!output.setPermissions(input.permissions())) {
      report.fail(dst, output.errorString());
      return;
    }
    if (!output.commit()) {
      report.fail(dst, output.errorString());
      return;
    }
    ++report.files;
  }

  void removeEntry(const QFileInfo &entry, OperationReport &report)
  {
    const QString path = entry.filePath();
    if (isLink(entry)) {
      report.fail(path, QObject::tr("Symbolic links and junctions are skipped."));
      return;
    }
    const bool directory = entry.isDir();
    if (!directory && !entry.isFile()) {
      report.fail(path, QObject::tr("Entry is unavailable or is a special file."));
      return;
    }
    const int failuresBefore = report.failed;
    if (directory)
      visitChildren(entry, report, [&](const QFileInfo &child) { removeEntry(child, report); });
    if (report.failed != failuresBefore) return;

    std::error_code error;
    if (!std::filesystem::remove(entry.filesystemFilePath(), error)) {
      report.fail(path, error ? QString::fromLocal8Bit(error.message().c_str())
        : QObject::tr("Entry is unavailable."));
      return;
    }
    if (directory)
      ++report.folders;
    else
      ++report.files;
  }

  QString summarize(const QString &summary, const OperationReport &report)
  {
    return report.failed == 0 ? summary
      : QObject::tr("%1 Failed entries: %2. %3").arg(summary).arg(report.failed).arg(report.firstError);
  }
}

QModelIndexList QDirDiffWidget::operationSelection(QTreeView *source) const
{
  auto *other = source == viewLeft_ ? viewRight_ : viewLeft_;
  auto *view = other->hasFocus() || other->isAncestorOf(QApplication::focusWidget()) ? other : source;
  return view->selectionModel()->selectedRows();
}

void QDirDiffWidget::copySelection(const QModelIndexList &selection, bool leftToRight)
{
  if (comparing_) return;

  struct SelectedEntry {
    QString path;
    DiffEntryKind kind;
  };
  std::vector<SelectedEntry> entries;
  for (const auto &index : selection) {
    const auto source = proxy_->mapToSource(index);
    if (!source.isValid()) continue;
    const auto *node = model_->nodeForIndex(source);
    const auto kind = node->kind(leftToRight);
    if (kind == DiffEntryKind::Missing) continue;
    const auto path = relPathForNode(node);
    if (std::none_of(entries.begin(), entries.end(), [&](const SelectedEntry &entry) { return entry.path == path; }))
      entries.push_back({ path, kind });
  }
  if (entries.empty()) return;

  const QString left = model_->leftRoot();
  const QString right = model_->rightRoot();
  const QString srcRoot = leftToRight ? left : right;
  const QString dstRoot = leftToRight ? right : left;
  OperationReport report;
  for (const auto &entry : entries) {
    if (std::any_of(entries.begin(), entries.end(), [&](const SelectedEntry &parent) {
        return parent.kind == DiffEntryKind::Directory && entry.path.startsWith(parent.path + '/');
        })) continue;

    const QString source = operationPath(srcRoot, entry.path, report);
    if (source.isEmpty()) continue;
    if (!matchesKind(QFileInfo(source), entry.kind)) {
      report.fail(source, tr("The source changed since the comparison."));
      continue;
    }
    const QString destination = operationPath(dstRoot, entry.path, report);
    if (destination.isEmpty()) continue;
    const bool same = source.compare(destination, pathCaseSensitivity) == 0;
    const bool overlapping = entry.kind == DiffEntryKind::Directory &&
      (sameOrUnder(source, destination) || sameOrUnder(destination, source));
    if (same || overlapping) {
      report.fail(destination, tr("Source and destination overlap."));
      continue;
    }
    copyEntry(QFileInfo(source), QFileInfo(destination), report);
  }
  startComparison(left, right, summarize(tr("Copied %1 files. Created %2 folders.")
    .arg(report.files).arg(report.folders), report));
}

void QDirDiffWidget::deleteEntry(const QModelIndex &idx, bool leftSide)
{
  const auto source = proxy_->mapToSource(idx);
  if (comparing_ || !source.isValid()) return;
  const auto *node = model_->nodeForIndex(source);
  const auto kind = node->kind(leftSide);
  if (kind == DiffEntryKind::Missing) return;
  const QString relative = relPathForNode(node);
  const QString left = model_->leftRoot();
  const QString right = model_->rightRoot();
  const QString root = leftSide ? left : right;
  const QFileInfo otherRoot(leftSide ? right : left);
  OperationReport report;
  const QString path = operationPath(root, relative, report);
  if (path.isEmpty()) {
    emit statusMessage(tr("Delete failed. %1").arg(report.firstError));
    return;
  }
  const QString otherPath = otherRoot.canonicalFilePath();
  if (!otherPath.isEmpty() && (sameOrUnder(otherPath, path) ||
      otherPath.compare(QFileInfo(root).canonicalFilePath(), pathCaseSensitivity) == 0)) {
    emit statusMessage(tr("Delete failed. The entry or its root is shared by both sides."));
    return;
  }

  QString message = tr("Permanently delete this entry from the %1 side?\n\n%2")
    .arg(leftSide ? tr("left") : tr("right"), QDir::toNativeSeparators(path));
  if (kind == DiffEntryKind::Directory)
    message += tr("\n\nThis includes the folder contents, including entries excluded from comparison.");
  if (QMessageBox::question(this, tr("Confirm Deletion"), message,
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
    return;

  const QString confirmedPath = operationPath(root, relative, report);
  if (!confirmedPath.isEmpty()) {
    if (confirmedPath != path || !matchesKind(QFileInfo(confirmedPath), kind))
      report.fail(confirmedPath, tr("The entry changed since the comparison."));
    else
      removeEntry(QFileInfo(confirmedPath), report);
  }
  startComparison(left, right, summarize(tr("Deleted %1 files and %2 folders.")
    .arg(report.files).arg(report.folders), report));
}

void QDirDiffWidget::expandAll(bool expand)
{
  if (expand) {
    viewLeft_->expandAll();
    viewRight_->expandAll();
  } else {
    viewLeft_->collapseAll();
    viewRight_->collapseAll();
  }
}

QString QDirDiffWidget::leftPath() const
{
  return headerLeft_->currentPath();
}

QString QDirDiffWidget::rightPath() const
{
  return headerRight_->currentPath();
}

QStringList QDirDiffWidget::exclusions()
{
  return QSettings().value(exclusionsSettingsKey).toStringList();
}

void QDirDiffWidget::setExclusions(const QStringList &patterns)
{
  QSettings().setValue(exclusionsSettingsKey, patterns);
}

void QDirDiffWidget::compareDirs()
{
  if (comparing_) return;
  const QString a = headerLeft_->currentPath().trimmed();
  const QString b = headerRight_->currentPath().trimmed();
  if (a.isEmpty() || b.isEmpty()) {
    emit statusMessage(tr("Please set both directories before comparing."));
    return;
  }

  startComparison(QDir(a).absolutePath(), QDir(b).absolutePath());
}

void QDirDiffWidget::startComparison(const QString &a, const QString &b, const QString &operation)
{
  comparing_ = true;
  operationSummary_ = operation;
  if (operation.isEmpty())
    emit compareStarted();
  emit statusMessage(operation.isEmpty() ? tr("Comparing...") : tr("Refreshing comparison..."));
  model_->clear();

  DiffOptions opts;
  opts.includeIdentical = true;
  opts.includeDirectories = true;
  for (const auto &pattern : exclusions())
    opts.ignorePatterns.push_back(pattern.toStdString());

  auto future = QtConcurrent::run([a, b, opts = std::move(opts)] {
    ComparisonResult result{ a, b, {}, {} };
    if (!QFileInfo(a).isDir()) {
      result.error = tr("Cannot open the left directory \"%1\".").arg(a);
      return result;
    }
    if (!QFileInfo(b).isDir()) {
      result.error = tr("Cannot open the right directory \"%1\".").arg(b);
      return result;
    }

    try {
      diffDirectories(
        QDir(a).filesystemPath(), QDir(b).filesystemPath(), opts,
        [&](const FileDiff &fd) { result.files.push_back(fd); });
    } catch (const std::filesystem::filesystem_error &error) {
      const QString path = QFileInfo(error.path1()).filePath();
      const QString reason = QString::fromLocal8Bit(error.code().message().c_str());
      result.error = path.isEmpty() ? reason : tr("Cannot scan \"%1\" (%2).").arg(path, reason);
    }
    return result;
    });
  watcher_->setFuture(future);
}

void QDirDiffWidget::onDiffFinished()
{
  QString message;
  auto future = watcher_->future();
  if (!future.isResultReadyAt(0)) {
    message = tr("Comparison failed before producing a result.");
  } else {
    const auto result = future.takeResult();
    if (!result.error.isEmpty()) {
      message = tr("Comparison failed. %1").arg(result.error);
    } else {
      model_->setDiffResults(result.files, result.leftRoot, result.rightRoot);
      viewLeft_->header()->resizeSections(QHeaderView::ResizeToContents);
      viewRight_->header()->resizeSections(QHeaderView::ResizeToContents);
      expandAll(true);
      const auto differences = std::count_if(result.files.begin(), result.files.end(),
        [](const FileDiff &entry) { return entry.type != DiffType::Identical; });
      message = tr("Done. %1 differences found.").arg(differences);
    }
  }

  if (!operationSummary_.isEmpty())
    message = tr("%1 %2").arg(operationSummary_, message);
  operationSummary_.clear();
  comparing_ = false;
  emit statusMessage(message);
  emit compareFinished();
}

// ─────────────────────────────────────────────────────────────────────────────

bool DirDiffNode::isDir() const
{
  return leftKind == DiffEntryKind::Directory || rightKind == DiffEntryKind::Directory;
}

DirDiffNode *DirDiffNode::findOrAddChild(const QString &childName)
{
  for (const auto &child : children)
    if (child->name == childName)
      return child.get();

  auto node = std::make_unique<DirDiffNode>();
  node->name = childName;
  node->parent = this;
  auto *ptr = node.get();
  children.push_back(std::move(node));
  return ptr;
}

QDirDiffModel::QDirDiffModel(QObject *parent)
  : QAbstractItemModel(parent)
  , root_(std::make_unique<DirDiffNode>())
{
}

void QDirDiffModel::clear()
{
  beginResetModel();
  root_ = std::make_unique<DirDiffNode>();
  endResetModel();
}

void QDirDiffModel::setDiffResults(const std::vector<FileDiff> &diffs, const QString &dirA, const QString &dirB)
{
  beginResetModel();
  root_ = std::make_unique<DirDiffNode>();
  dirA_ = dirA;
  dirB_ = dirB;
  for (const auto &diff : diffs) {
    const QString relative = QFileInfo(diff.relativePath).filePath();
    const auto parts = relative.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) continue;

    auto *node = root_.get();
    for (const auto &part : parts)
      node = node->findOrAddChild(part);
    node->leftKind = diff.leftKind;
    node->rightKind = diff.rightKind;
    node->diffType = diff.type;
  }

  updateDirectories(root_.get());
  endResetModel();
}

void QDirDiffModel::updateDirectories(DirDiffNode *node)
{
  for (const auto &child : node->children) {
    updateDirectories(child.get());
    if (node->leftKind == DiffEntryKind::Directory && node->rightKind == DiffEntryKind::Directory &&
        child->diffType != DiffType::Identical) {
      node->diffType = DiffType::Modified;
    }
  }
}

DirDiffNode *QDirDiffModel::nodeForIndex(const QModelIndex &idx) const
{
  if (!idx.isValid()) return root_.get();
  return static_cast<DirDiffNode *>(idx.internalPointer());
}

QModelIndex QDirDiffModel::index(int row, int column, const QModelIndex &parent) const
{
  if (row < 0 || column < 0) return {};
  DirDiffNode *parentNode = nodeForIndex(parent);
  if (row >= static_cast<int>(parentNode->children.size())) return {};
  return createIndex(row, column, parentNode->children[row].get());
}

QModelIndex QDirDiffModel::parent(const QModelIndex &child) const
{
  if (!child.isValid()) return {};
  DirDiffNode *node = nodeForIndex(child);
  DirDiffNode *parentNode = node->parent;
  if (!parentNode || parentNode == root_.get()) return {};

  // Find parentNode's row in its own parent
  DirDiffNode *grandParent = parentNode->parent;
  if (!grandParent) return {};
  for (int i = 0; i < static_cast<int>(grandParent->children.size()); ++i) {
    if (grandParent->children[i].get() == parentNode)
      return createIndex(i, 0, parentNode);
  }
  return {};
}

int QDirDiffModel::rowCount(const QModelIndex &parent) const
{
  return static_cast<int>(nodeForIndex(parent)->children.size());
}

int QDirDiffModel::columnCount(const QModelIndex &) const
{
  return static_cast<int>(DiffColumn::Count);   // 3
}

QVariant QDirDiffModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid()) return {};
  const DirDiffNode *node = nodeForIndex(index);
  const auto col = static_cast<DiffColumn>(index.column());
  const bool nameColumn = col == DiffColumn::LeftName || col == DiffColumn::RightName;
  const auto kind = node->kind(col == DiffColumn::LeftName);
  const bool exists = kind != DiffEntryKind::Missing;

  switch (role) {
  case Qt::DisplayRole:
    if (col == DiffColumn::DiffMark)
      return markForDiff(node->diffType);
    if (nameColumn && exists)
      return node->name;
    return {};
  case Qt::ToolTipRole:
    if (nameColumn && !exists)
      return {};
    return node->name + " is " + diffType2Str(node->diffType);
  case Qt::TextAlignmentRole:
    if (col == DiffColumn::DiffMark)
      return int(Qt::AlignHCenter | Qt::AlignVCenter);
    return {};
  case DiffTypeRole:
    return static_cast<int>(node->diffType);
  case IsDirRole:
    return node->isDir();
  case TypeConflictRole:
    return node->isDir() && (node->leftKind == DiffEntryKind::File || node->rightKind == DiffEntryKind::File);
  case Qt::DecorationRole:
    if (nameColumn && exists) {
      if (iconFolder_.isNull()) {
        iconFolder_ = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
        iconFile_ = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
      }
      return kind == DiffEntryKind::Directory ? iconFolder_ : iconFile_;
    }
    return {};
  }
  return {};
}

QVariant QDirDiffModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
  switch (static_cast<DiffColumn>(section)) {
  case DiffColumn::LeftName:  return dirA_.isEmpty() ? QStringLiteral("Left") : dirA_;
  case DiffColumn::DiffMark:  return QStringLiteral("");
  case DiffColumn::RightName: return dirB_.isEmpty() ? QStringLiteral("Right") : dirB_;
  default: return {};
  }
}

// ─────────────────────────────────────────────────────────────────────────────

QDirDiffProxyModel::QDirDiffProxyModel(QObject *parent)
  : QSortFilterProxyModel(parent)
{
  setRecursiveFilteringEnabled(false);  // we handle recursion manually
}

void QDirDiffProxyModel::setShowIdentical(bool show) { showIdentical_ = show; invalidateFilter(); }
void QDirDiffProxyModel::setShowModified(bool show) { showModified_ = show; invalidateFilter(); }
void QDirDiffProxyModel::setShowAdded(bool show) { showAdded_ = show; invalidateFilter(); }
void QDirDiffProxyModel::setShowRemoved(bool show) { showRemoved_ = show; invalidateFilter(); }

bool QDirDiffProxyModel::diffTypeAccepted(DiffType t) const
{
  switch (t) {
  case DiffType::Identical: return showIdentical_;
  case DiffType::Modified:  return showModified_;
  case DiffType::Added:     return showAdded_;
  case DiffType::Removed:   return showRemoved_;
  }
  return true;
}

bool QDirDiffProxyModel::rowOrAnyChildAccepted(int sourceRow, const QModelIndex &sourceParent) const
{
  const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
  const auto diffType = static_cast<DiffType>(idx.data(DiffTypeRole).toInt());

  // For directories: accept if any child is accepted (so we don't hide
  // a dir that contains visible children)
  if (idx.data(IsDirRole).toBool()) {
    const int childCount = sourceModel()->rowCount(idx);
    if ((childCount == 0 || idx.data(TypeConflictRole).toBool()) && diffTypeAccepted(diffType))
      return true;
    for (int i = 0; i < childCount; ++i)
      if (rowOrAnyChildAccepted(i, idx))
        return true;
    return false;
  }

  return diffTypeAccepted(diffType);
}

bool QDirDiffProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
  return rowOrAnyChildAccepted(sourceRow, sourceParent);
}
