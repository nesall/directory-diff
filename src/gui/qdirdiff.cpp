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
#include <optional>

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
        const QModelIndex idx = v->indexAt(pos);
        if (!idx.isValid() || !idx.data().isValid()) return;

        auto selectedIndexes = v->selectionModel()->selectedRows();

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
        const bool existsOnSide = v == viewLeft_ ? node->existsOnLeft : node->existsOnRight;
        actDel->setEnabled(existsOnSide && selectedIndexes.size() == 1);
        connect(actLR, &QAction::triggered, this, [this] { copySelection(true);  });
        connect(actRL, &QAction::triggered, this, [this] { copySelection(false); });
        connect(actDel, &QAction::triggered, this, [this, idx, v] { deleteFile(idx, v == viewLeft_); });
        connect(actView, &QAction::triggered, this, [this, idx] {
          DirDiffNode *node = model_->nodeForIndex(proxy_->mapToSource(idx));
          if (!node || node->isDir) {
            return;
          }
          const QString rel = relPathForNode(node);
          //auto *dialog = new QFileDiffDialog(this);
          //dialog->setAttribute(Qt::WA_DeleteOnClose);
          //dialog->showDiff(leftPath() + '/' + rel, rightPath() + '/' + rel);
          emit compareFileRequested(QDir(model_->leftRoot()).filePath(rel), QDir(model_->rightRoot()).filePath(rel),
            !node->existsOnLeft, !node->existsOnRight);
          });

        menu.exec(v->viewport()->mapToGlobal(pos));
      });
  }

  auto *scLR = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
  auto *scRL = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this);
  connect(scLR, &QShortcut::activated, this, [this]
    {
      const QModelIndex idx = viewLeft_->currentIndex().isValid() ? viewLeft_->currentIndex() : viewRight_->currentIndex();
      if (idx.isValid()) copySelection(true);
    }
  );
  connect(scRL, &QShortcut::activated, this, [this]
    {
      const QModelIndex idx = viewLeft_->currentIndex().isValid() ? viewLeft_->currentIndex() : viewRight_->currentIndex();
      if (idx.isValid()) copySelection(false);
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

QString QDirDiffWidget::relPathForNode(DirDiffNode *node) const
{
  QStringList parts;
  for (DirDiffNode *n = node; n && n->parent; n = n->parent)
    parts.prepend(n->name);
  return parts.join('/');
}

void QDirDiffWidget::copyFile(const QModelIndex &idx, bool leftToRight)
{
  DirDiffNode *node = model_->nodeForIndex(proxy_->mapToSource(idx));
  if (!node) return;
  if (node->isDir) {
    copyDir(idx, leftToRight);
    return;
  }

  const QString rel = relPathForNode(node);
  const QString src = (leftToRight ? leftPath() : rightPath()) + '/' + rel;
  const QString dst = (leftToRight ? rightPath() : leftPath()) + '/' + rel;

  QFileInfo dstInfo(dst);
  QDir().mkpath(dstInfo.absolutePath());

  if (QFile::exists(dst))
    QFile::remove(dst);

  if (!QFile::copy(src, dst)) {
    emit statusMessage(tr("Copy failed: %1 → %2").arg(src, dst));
    return;
  }

  emit statusMessage(tr("Copied: %1").arg(rel));

  // Refresh — re-run the diff so the row updates its DiffType
  // TODO: optimize - only update copied items
  compareDirs();
}

void QDirDiffWidget::copyDir(const QModelIndex &idx, bool leftToRight)
{
  DirDiffNode *node = model_->nodeForIndex(proxy_->mapToSource(idx));
  if (!node || !node->isDir) return;

  const QString rel = relPathForNode(node);
  const QString srcRoot = leftToRight ? leftPath() : rightPath();
  const QString dstRoot = leftToRight ? rightPath() : leftPath();
  const QString srcDir = srcRoot + '/' + rel;
  const QString dstDir = dstRoot + '/' + rel;

  if (!QDir(srcDir).exists()) {
    emit statusMessage(tr("Source directory does not exist: %1").arg(srcDir));
    return;
  }

  QDir().mkpath(dstDir);

  std::function<void(const QString &, const QString &)> copyRecursive;
  copyRecursive = [&](const QString &src, const QString &dst)
    {
      QDir srcQDir(src);
      for (const QFileInfo &entry : srcQDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
        QString dstPath = dst + '/' + entry.fileName();
        if (entry.isDir()) {
          QDir().mkpath(dstPath);
          copyRecursive(entry.absoluteFilePath(), dstPath);
        } else {
          if (QFile::exists(dstPath))
            QFile::remove(dstPath);
          QFile::copy(entry.absoluteFilePath(), dstPath);
        }
      }
    };

  copyRecursive(srcDir, dstDir);

  emit statusMessage(tr("Copied directory: %1").arg(rel));
  compareDirs();
}

void QDirDiffWidget::copySelection(bool leftToRight)
{
  QTreeView *sourceView = leftToRight ? viewLeft_ : viewRight_;
  const auto selectedIndexes = sourceView->selectionModel()->selectedRows();
  if (selectedIndexes.isEmpty()) return;
  for (const QModelIndex &idx : selectedIndexes) {
    // copy file or dir
    DirDiffNode *node = model_->nodeForIndex(proxy_->mapToSource(idx));
    if (!node || !(leftToRight ? node->existsOnLeft : node->existsOnRight)) continue;
    if (node->isDir)
      copyDir(idx, leftToRight);
    else
      copyFile(idx, leftToRight);
  }
}

void QDirDiffWidget::deleteFile(const QModelIndex &idx, bool leftSide)
{
  DirDiffNode *node = model_->nodeForIndex(proxy_->mapToSource(idx));
  if (!node || !(leftSide ? node->existsOnLeft : node->existsOnRight)) return;
  if (QMessageBox::question(this, tr("Confirm Deletion"),
    tr("Are you sure you want to delete '%1' from the %2 side?")
    .arg(relPathForNode(node))
    .arg(leftSide ? tr("left") : tr("right")),
    QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Cancel) {
    return;
  }
    
  const QString rel = relPathForNode(node);
  const QString targetDir = leftSide ? leftPath() : rightPath();
  const QString targetPath = targetDir + '/' + rel;
  if (node->isDir) {
    QDir dir(targetPath);
    if (!dir.exists()) {
      emit statusMessage(tr("Directory does not exist: %1").arg(targetPath));
      return;
    }
    if (!dir.removeRecursively()) {
      emit statusMessage(tr("Failed to delete directory: %1").arg(targetPath));
      return;
    }
    emit statusMessage(tr("Deleted directory: %1").arg(rel));
  } else {
    if (!QFile::exists(targetPath)) {
      emit statusMessage(tr("File does not exist: %1").arg(targetPath));
      return;
    }
    if (!QFile::remove(targetPath)) {
      emit statusMessage(tr("Failed to delete file: %1").arg(targetPath));
      return;
    }
    emit statusMessage(tr("Deleted file: %1").arg(rel));
  }
  compareDirs();
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
  const QString a = headerLeft_->currentPath().trimmed();
  const QString b = headerRight_->currentPath().trimmed();
  if (a.isEmpty() || b.isEmpty()) {
    emit statusMessage(tr("Please set both directories before comparing."));
    return;
  }

  emit compareStarted();
  emit statusMessage(tr("Comparing..."));
  model_->clear();

  DiffOptions opts;
  opts.includeIdentical = true;
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
  if (!watcher_->future().isResultReadyAt(0)) {
    emit statusMessage(tr("Comparison failed before producing a result."));
    emit compareFinished();
    return;
  }

  const auto result = watcher_->result();
  if (!result.error.isEmpty()) {
    emit statusMessage(tr("Comparison failed. %1").arg(result.error));
    emit compareFinished();
    return;
  }

  model_->setDiffResults(result.files, result.leftRoot, result.rightRoot);
  viewLeft_->header()->resizeSections(QHeaderView::ResizeToContents);
  viewRight_->header()->resizeSections(QHeaderView::ResizeToContents);
  expandAll(true);
  emit compareFinished();
  emit statusMessage(tr("Done. %1 differences found.").arg(result.files.size()));
}

// ─────────────────────────────────────────────────────────────────────────────

DiffType DirDiffNode::properChildDiff() const
{
  std::optional<DiffType> proper;
  for (const auto &c : children) {
    DiffType ct = c->isDir ? c->properChildDiff() : c->diffType;
    if (!proper.has_value()) {
      proper = ct;
    } else if (proper.value() != ct) {
      return DiffType::Modified;
    }
    if (proper == DiffType::Modified) break;
  }
  return proper.value_or(DiffType::Identical);
}

DirDiffNode *DirDiffNode::findOrAddChild(const QString &childName, bool dir)
{
  for (const auto &c : children)
    if (c->name == childName)
      return c.get();
  auto node = std::make_unique<DirDiffNode>();
  node->name = childName;
  node->isDir = dir;
  node->parent = this;
  node->diffType = DiffType::Identical;
  auto *ptr = node.get();
  children.push_back(std::move(node));
  return ptr;
}

// ─────────────────────────────────────────────────────────────────────────────

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
  root_->existsOnLeft = true;
  root_->existsOnRight = true;

  for (const auto &fd : diffs) {
    const QString relPath = QFileInfo(fd.relativePath).filePath();
    QStringList parts = relPath.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) continue;

    DirDiffNode *cur = root_.get();
    for (int i = 0; i < parts.size(); ++i) {
      bool isLast = (i == parts.size() - 1);
      cur = cur->findOrAddChild(parts[i], !isLast);
      cur->existsOnLeft |= fd.type != DiffType::Added;
      cur->existsOnRight |= fd.type != DiffType::Removed;
      if (isLast) {
        cur->diffType = fd.type;
      }
    }
  }

  for (const auto &child : root_->children)
    updateDirectories(child.get(), child->name);

  endResetModel();
}

void QDirDiffModel::updateDirectories(DirDiffNode *node, const QString &relativePath)
{
  if (node->isDir) {
    // A folder can be present on a side that contributed no file results.
    if (!node->existsOnLeft && node->parent->existsOnLeft)
      node->existsOnLeft = QFileInfo(QDir(dirA_).filePath(relativePath)).isDir();
    if (!node->existsOnRight && node->parent->existsOnRight)
      node->existsOnRight = QFileInfo(QDir(dirB_).filePath(relativePath)).isDir();
  }

  for (const auto &child : node->children)
    updateDirectories(child.get(), relativePath + '/' + child->name);
  if (node->isDir)
    node->diffType = node->properChildDiff();
}


// ── QAbstractItemModel ────────────────────────────────────────────────────────

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
  const bool exists = col == DiffColumn::LeftName ? node->existsOnLeft : node->existsOnRight;

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
    return node->isDir;
  case Qt::DecorationRole:
    if (nameColumn && exists) {
      if (iconFolder_.isNull()) {
        iconFolder_ = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
        iconFile_ = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
      }
      return node->isDir ? iconFolder_ : iconFile_;
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
