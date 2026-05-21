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
  if (path.trimmed().isEmpty()) return;
  QSettings s;
  QStringList h = s.value(dirPanel_ ? DIRDIFF_HISTORY_KEY : FILEDIFF_HISTORY_KEY).toStringList();
  h.removeAll(path);
  h.append(path);
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
  for (const QString &p : h)
    pathCombo_->addItem(p);
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
  class QCompactDelegate : public QStyledItemDelegate {
  public:
    QCompactDelegate(QWidget *parent) : QStyledItemDelegate(parent) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
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

  viewLeft_ = new QTreeView(this);
  viewRight_ = new QTreeView(this);
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

  watcher_ = new QFutureWatcher<std::vector<FileDiff>>(this);
  connect(watcher_, &QFutureWatcher<std::vector<FileDiff>>::finished, this, &QDirDiffWidget::onDiffFinished);
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

  auto connectSyncSelection = [this](const QModelIndex &current, const QModelIndex &, QTreeView *source, QTreeView *target)
    {
      connect(source->selectionModel(), &QItemSelectionModel::currentChanged, this, [this, target]
      (const QModelIndex &current, const QModelIndex &)
        {
          if (syncingSelection_) return;
          syncingSelection_ = true;
          target->selectionModel()->setCurrentIndex(current,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
          target->scrollTo(current);
          syncingSelection_ = false;
        }
      );
    };
  connectSyncSelection(viewLeft_->selectionModel()->currentIndex(), {}, viewLeft_, viewRight_);
  connectSyncSelection(viewRight_->selectionModel()->currentIndex(), {}, viewRight_, viewLeft_);

  for (QTreeView *v : { viewLeft_, viewRight_ }) {
    v->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(v, &QWidget::customContextMenuRequested, this, [this, v](const QPoint &pos)
      {
        const QModelIndex idx = v->indexAt(pos);
        if (!idx.isValid()) return;

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
        //actLR->setEnabled(node->diffType != DiffType::Added);   // Added means only exists in right
        //actRL->setEnabled(node->diffType != DiffType::Removed); // Removed means only exists in left
        //actDel->setEnabled(node->diffType != DiffType::Identical);
        actDel->setEnabled(selectedIndexes.size() == 1);
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
          emit compareFileRequested(leftPath() + '/' + rel, rightPath() + '/' + rel);
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
    if (!node) continue;
    if (node->isDir)
      copyDir(idx, leftToRight);
    else
      copyFile(idx, leftToRight);
  }
}

void QDirDiffWidget::deleteFile(const QModelIndex &idx, bool leftSide)
{
  DirDiffNode *node = model_->nodeForIndex(proxy_->mapToSource(idx));
  if (!node) return;
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

  auto future = QtConcurrent::run([a, b]() -> std::vector<FileDiff> {
    std::vector<FileDiff> results;
    DiffOptions opts;
    opts.useHash = true;
    opts.ignoreHidden = false;
    opts.includeIdentical = true;
    diffDirectories(
      a.toStdString(), b.toStdString(), opts,
      [&](const FileDiff &fd) { results.push_back(fd); });
    return results;
    });
  watcher_->setFuture(future);
}

void QDirDiffWidget::onDiffFinished()
{
  const auto results = watcher_->result();
  model_->setDiffResults(results, leftPath(), rightPath());
  viewLeft_->header()->resizeSections(QHeaderView::ResizeToContents);
  viewRight_->header()->resizeSections(QHeaderView::ResizeToContents);
  expandAll(true);
  emit compareFinished();
  emit statusMessage(tr("Done. %1 differences found.").arg(results.size()));
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

  for (const auto &fd : diffs) {
    QString relPath = QString::fromStdString(fd.relativePath.string());
    relPath.replace('\\', '/');
    QStringList parts = relPath.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) continue;

    DirDiffNode *cur = root_.get();
    for (int i = 0; i < parts.size(); ++i) {
      bool isLast = (i == parts.size() - 1);
      cur = cur->findOrAddChild(parts[i], !isLast);
      if (isLast) {
        cur->diffType = fd.type;
      }
    }
  }

  propagateDiffTypes(root_.get());

  endResetModel();
}

void QDirDiffModel::propagateDiffTypes(DirDiffNode *node)
{
  for (const auto &c : node->children)
    propagateDiffTypes(c.get());

  if (node->isDir) {
    node->diffType = node->properChildDiff();
  }
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

  switch (role) {
  case Qt::DisplayRole: {
    QString text;
    if (col == DiffColumn::DiffMark) {
      text = markForDiff(node->diffType);
    } else if (col == DiffColumn::LeftName) {
      text = (node->diffType != DiffType::Added) ? node->name : QString();
    } else if (col == DiffColumn::RightName) {
      text = (node->diffType != DiffType::Removed) ? node->name : QString();
    }
    return text;
  }
  case Qt::ToolTipRole:
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
    if (col == DiffColumn::LeftName || col == DiffColumn::RightName) {
      if (iconFolder_.isNull()) {
        iconFolder_ = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
        iconFile_ = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
      }
      if (node->isDir)
        return iconFolder_;
      else {
        if (col == DiffColumn::LeftName && node->diffType != DiffType::Added) {
          return iconFile_;
        } else if (col == DiffColumn::RightName && node->diffType != DiffType::Removed) {
          return iconFile_;
        }
      }
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