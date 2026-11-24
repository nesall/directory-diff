#include "qdirdiff.h"
#include "filemutation.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
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
#include <QHash>
#include <QSet>
#include <QLabel>
#include <QLocale>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QStyle>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QMessageBox>
#include <algorithm>
#include <mutex>

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
  pathCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
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

struct QDirDiffJobState {
  struct FileProgress {
    QString path;
    int completed = 0;
  };

  std::stop_source stop;
  DirectoryDiffProgress comparison;

  void setFileProgress(const QString &path, int completed)
  {
    const std::lock_guard lock(mutex_);
    fileProgress_ = { path, completed };
  }

  FileProgress fileProgress() const
  {
    const std::lock_guard lock(mutex_);
    return fileProgress_;
  }

private:
  mutable std::mutex mutex_;
  FileProgress fileProgress_;
};

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

  progressRow_ = new QWidget(this);
  auto *progressLayout = new QHBoxLayout(progressRow_);
  progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLabel_ = new QLabel(progressRow_);
  progressLabel_->setTextFormat(Qt::PlainText);
  progressLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  progressBar_ = new QProgressBar(progressRow_);
  progressBar_->setAccessibleName(tr("Directory job progress"));
  cancelButton_ = new QPushButton(tr("Cancel"), progressRow_);
  progressLayout->addWidget(progressLabel_, 1);
  progressLayout->addWidget(progressBar_);
  progressLayout->addWidget(cancelButton_);
  layout->addWidget(progressRow_);
  progressRow_->hide();

  progressTimer_ = new QTimer(this);
  progressTimer_->setInterval(100);
  connect(progressTimer_, &QTimer::timeout, this, &QDirDiffWidget::updateProgress);
  connect(cancelButton_, &QPushButton::clicked, this, [this] {
    if (!job_) return;
    job_->stop.request_stop();
    cancelButton_->setEnabled(false);
    updateProgress();
  });

  watcher_ = new QFutureWatcher<ComparisonResult>(this);
  connect(watcher_, &QFutureWatcher<ComparisonResult>::finished, this, &QDirDiffWidget::onDiffFinished);
  operationWatcher_ = new QFutureWatcher<OperationResult>(this);
  connect(operationWatcher_, &QFutureWatcher<OperationResult>::finished, this, &QDirDiffWidget::onFileOperationFinished);
  connect(headerLeft_, &QGenericPanelHeader::returnPressed, this, [this] { compareDirs(); });
  connect(headerRight_, &QGenericPanelHeader::returnPressed, this, [this] { compareDirs(); });
}

QDirDiffWidget::~QDirDiffWidget()
{
  if (job_) job_->stop.request_stop();
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
    connect(v, &QTreeView::doubleClicked, this, &QDirDiffWidget::compareFile);
    connect(v, &QWidget::customContextMenuRequested, this, [this, v](const QPoint &pos)
      {
        if (job_) return;
        const QModelIndex idx = v->indexAt(pos);
        if (!idx.isValid() || !idx.data().isValid()) return;

        auto selectedIndexes = v->selectionModel()->selectedRows();
        if (!v->selectionModel()->isSelected(idx))
          selectedIndexes = { idx };

        QMenu menu;
        auto *actView = menu.addAction(tr("Compare files"), this, [this, index = QPersistentModelIndex(idx)] {
          compareFile(index);
        });
        menu.addSeparator();
        auto *actLR = menu.addAction(QIcon(":/arrow-right-from-line"), tr("Copy Left to Right"), QKeySequence("Ctrl+W"));
        auto *actRL = menu.addAction(QIcon(":/arrow-left-from-line"), tr("Copy Right to Left"), QKeySequence("Ctrl+Q"));
        DirDiffNode *node = model_->nodeForIndex(proxy_->mapToSource(idx));
        const bool leftSide = v == viewLeft_;
        const bool existsOnSide = node->kind(leftSide) != DiffEntryKind::Missing;
        if (existsOnSide) {
          const auto addClipboardAction = [&](const QString &label, const QString &value) {
            menu.addAction(label, this, [value] { QApplication::clipboard()->setText(value); });
          };
          const QString root = leftSide ? model_->leftRoot() : model_->rightRoot();
          const QString relative = relPathForNode(node, leftSide);
          menu.addSeparator();
          addClipboardAction(tr("Copy Name"), node->nameOnSide(leftSide));
          addClipboardAction(tr("Copy Full Path"), QDir::toNativeSeparators(QDir(root).filePath(relative)));
          addClipboardAction(tr("Copy Relative Path"), QDir::toNativeSeparators(relative));
        }
        menu.addSeparator();
        static QIcon iconDel; 
        if (iconDel.isNull())
          iconDel = QApplication::style()->standardIcon(QStyle::SP_TrashIcon);
        auto *actDel = menu.addAction(iconDel, tr("Delete"));

        actView->setEnabled(!node->isDir());
        if (actView->isEnabled()) {
          menu.setDefaultAction(actView);
          QFont font = actView->font();
          font.setBold(true);
          actView->setFont(font);
        }
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

QString QDirDiffWidget::relPathForNode(const DirDiffNode *node, bool left) const
{
  QStringList parts;
  for (auto *n = node; n && n->parent; n = n->parent)
    parts.prepend(n->nameOnSide(left));
  return parts.join('/');
}

void QDirDiffWidget::compareFile(const QModelIndex &index)
{
  if (job_ || !index.data().isValid()) return;
  const auto source = proxy_->mapToSource(index);
  if (!source.isValid()) return;
  const auto *node = model_->nodeForIndex(source);
  if (node->isDir()) return;

  emit compareFileRequested(QDir(model_->leftRoot()).filePath(relPathForNode(node, true)),
    QDir(model_->rightRoot()).filePath(relPathForNode(node, false)),
    node->leftKind == DiffEntryKind::Missing, node->rightKind == DiffEntryKind::Missing);
}

namespace {
  struct OperationReport {
    QDirDiffJobState *job = nullptr;
    int files = 0;
    int folders = 0;
    int failed = 0;
    QString firstError;

    void fail(const QString &path, const QString &message)
    {
      if (failed++ == 0)
        firstError = QObject::tr("%1 (%2)").arg(path, message);
    }

    bool stopped() const { return job && job->stop.stop_requested(); }

    void visit(const QString &path) const { job->setFileProgress(path, files + folders); }
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
    for (; !error && entry != end && !report.stopped(); entry.increment(error))
      visit(QFileInfo(entry->path()));
    if (error)
      report.fail(directory.filePath(), QString::fromLocal8Bit(error.message().c_str()));
  }

  void copyEntry(const QFileInfo &source, const QFileInfo &destination, const DirDiffNode *snapshot,
    bool leftToRight, OperationReport &report, CopiedEntries &copied)
  {
    if (report.stopped()) return;
    const QString src = source.filePath();
    const QString dst = destination.filePath();
    report.visit(src);
    if (isLink(source) || isLink(destination)) {
      report.fail(isLink(source) ? src : dst, QObject::tr("Symbolic links and junctions are skipped."));
      return;
    }
    if (snapshot && !matchesKind(source, snapshot->kind(leftToRight))) {
      report.fail(src, QObject::tr("The source changed since the comparison."));
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
      QHash<QString, const DirDiffNode*> children;
      if (snapshot) {
        for (const auto &child : snapshot->children) {
          if (report.stopped()) return;
          if (child->kind(leftToRight) != DiffEntryKind::Missing)
            children.insert(child->nameOnSide(leftToRight), child.get());
        }
      }
      visitChildren(source, report, [&](const QFileInfo &child) {
        const auto *match = children.take(child.fileName());
        const QString name = match ? match->nameOnSide(!leftToRight) : child.fileName();
        copyEntry(child, QFileInfo(QDir(dst).filePath(name)), match, leftToRight, report, copied);
      });
      if (report.stopped()) return;
      for (auto child = children.cbegin(); child != children.cend(); ++child)
        report.fail(QDir(src).filePath(child.key()), QObject::tr("The source changed since the comparison."));
      if (snapshot) copied.insert(snapshot, std::nullopt);
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
    const bool expectedFile = snapshot && snapshot->kind(!leftToRight) == DiffEntryKind::File;
    if (destination.exists() != expectedFile) {
      report.fail(dst, QObject::tr("Destination differs from the comparison. Compare again before replacing it."));
      return;
    }
    if (expectedFile && !snapshot->fingerprint(!leftToRight)) {
      report.fail(dst, QObject::tr("Destination could not be verified during comparison."));
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
    while (!report.stopped()) {
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
    if (report.stopped()) return;
    if (!output.setPermissions(input.permissions())) {
      report.fail(dst, output.errorString());
      return;
    }
    const QFileInfo current(dst);
    const bool verified = !isLink(current) && current.exists() == expectedFile &&
      (!expectedFile || fingerprintFile(current.filesystemFilePath(), report.job->stop.get_token()) ==
        snapshot->fingerprint(!leftToRight));
    if (report.stopped()) return;
    if (!verified) {
      report.fail(dst, QObject::tr("Destination changed since the comparison. Compare again before replacing it."));
      return;
    }
    if (!output.commit()) {
      report.fail(dst, output.errorString());
      return;
    }
    ++report.files;
    if (snapshot) {
      const auto stop = report.job->stop.get_token();
      const auto fingerprint = fingerprintFile(destination.filesystemFilePath(), stop);
      const auto sourceFingerprint = fingerprintFile(source.filesystemFilePath(), stop);
      if (report.stopped()) return;
      if (!fingerprint || fingerprint != sourceFingerprint) {
        report.fail(dst, QObject::tr("The copied file could not be verified."));
        return;
      }
      copied.insert(snapshot, fingerprint);
    }
  }

  void removeEntry(const QFileInfo &entry, OperationReport &report)
  {
    if (report.stopped()) return;
    const QString path = entry.filePath();
    report.visit(path);
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
    if (report.stopped() || report.failed != failuresBefore) return;
    report.visit(path);

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

  bool isSharedDeletePath(const QString &path, const QString &root, const QString &otherRoot)
  {
    const QString otherPath = QFileInfo(otherRoot).canonicalFilePath();
    return !otherPath.isEmpty() && (sameOrUnder(otherPath, path) ||
      otherPath.compare(QFileInfo(root).canonicalFilePath(), pathCaseSensitivity) == 0);
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
  if (job_) return;

  struct SelectedEntry {
    QString path;
    QString destination;
    std::shared_ptr<const DirDiffNode> node;
  };
  std::vector<SelectedEntry> entries;
  entries.reserve(selection.size());
  QSet<QString> selectedPaths;
  QSet<QString> selectedFolders;
  for (const auto &index : selection) {
    const auto source = proxy_->mapToSource(index);
    if (!source.isValid()) continue;
    auto node = model_->snapshotForIndex(source);
    const auto kind = node->kind(leftToRight);
    if (kind == DiffEntryKind::Missing) continue;
    const auto path = relPathForNode(node.get(), leftToRight);
    if (selectedPaths.contains(path)) continue;
    selectedPaths.insert(path);
    if (kind == DiffEntryKind::Directory) selectedFolders.insert(path);
    entries.push_back({ path, relPathForNode(node.get(), !leftToRight), std::move(node) });
  }
  if (entries.empty()) return;

  const QString srcRoot = leftToRight ? model_->leftRoot() : model_->rightRoot();
  const QString dstRoot = leftToRight ? model_->rightRoot() : model_->leftRoot();
  startFileOperation(Job::Copy, [srcRoot, dstRoot, leftToRight, entries = std::move(entries),
      selectedFolders = std::move(selectedFolders)](QDirDiffJobState &job) {
    OperationReport report{ &job };
    CopiedEntries copied;
    for (const auto &entry : entries) {
      if (report.stopped()) break;
      bool covered = false;
      QString parent = entry.path;
      for (auto slash = parent.lastIndexOf('/'); slash >= 0; slash = parent.lastIndexOf('/')) {
        parent.truncate(slash);
        if (selectedFolders.contains(parent)) {
          covered = true;
          break;
        }
      }
      if (covered) continue;

      report.visit(QDir(srcRoot).filePath(entry.path));
      const QString source = operationPath(srcRoot, entry.path, report);
      if (source.isEmpty()) continue;
      const auto kind = entry.node->kind(leftToRight);
      if (!matchesKind(QFileInfo(source), kind)) {
        report.fail(source, tr("The source changed since the comparison."));
        continue;
      }
      const QString destination = operationPath(dstRoot, entry.destination, report);
      if (destination.isEmpty()) continue;
      const bool same = source.compare(destination, pathCaseSensitivity) == 0;
      const bool overlapping = kind == DiffEntryKind::Directory &&
        (sameOrUnder(source, destination) || sameOrUnder(destination, source));
      if (same || overlapping) {
        report.fail(destination, tr("Source and destination overlap."));
        continue;
      }
      copyEntry(QFileInfo(source), QFileInfo(destination), entry.node.get(), leftToRight, report, copied);
    }
    const QString summary = tr("Copied %1 files. Created %2 folders.").arg(report.files).arg(report.folders);
    const QString sourceRoot = QFileInfo(srcRoot).canonicalFilePath();
    const QString destinationRoot = QFileInfo(dstRoot).canonicalFilePath();
    // With nested roots, a destination can also appear at another row in the source tree.
    const bool overlappingRoots = !sourceRoot.isEmpty() && !destinationRoot.isEmpty() &&
      (sameOrUnder(sourceRoot, destinationRoot) || sameOrUnder(destinationRoot, sourceRoot));
    return OperationResult{ summarize(report.stopped() ? tr("Copy canceled. %1").arg(summary) : summary, report),
      report.failed != 0 || report.stopped() || overlappingRoots, std::move(copied), leftToRight };
  });
}

void QDirDiffWidget::deleteEntry(const QModelIndex &idx, bool leftSide)
{
  const auto source = proxy_->mapToSource(idx);
  if (job_ || !source.isValid()) return;
  const auto *node = model_->nodeForIndex(source);
  const auto kind = node->kind(leftSide);
  if (kind == DiffEntryKind::Missing) return;
  const QString relative = relPathForNode(node, leftSide);
  const QString left = model_->leftRoot();
  const QString right = model_->rightRoot();
  const QString root = leftSide ? left : right;
  const QString otherRoot = leftSide ? right : left;
  OperationReport report;
  const QString path = operationPath(root, relative, report);
  if (path.isEmpty()) {
    showStatus(tr("Delete failed. %1").arg(report.firstError));
    return;
  }
  if (isSharedDeletePath(path, root, otherRoot)) {
    showStatus(tr("Delete failed. The entry or its root is shared by both sides."));
    return;
  }

  QString message = tr("Permanently delete this entry from the %1 side?\n\n%2")
    .arg(leftSide ? tr("left") : tr("right"), QDir::toNativeSeparators(path));
  if (kind == DiffEntryKind::Directory)
    message += tr("\n\nThis includes the folder contents, including entries excluded from comparison.");
  if (QMessageBox::question(this, tr("Confirm Deletion"), message,
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
    return;

  startFileOperation(Job::Delete, [root, otherRoot, relative, path, kind](QDirDiffJobState &job) {
    OperationReport report{ &job };
    if (!report.stopped()) {
      report.visit(path);
      const QString confirmedPath = operationPath(root, relative, report);
      if (!confirmedPath.isEmpty()) {
        if (confirmedPath != path || !matchesKind(QFileInfo(confirmedPath), kind))
          report.fail(confirmedPath, tr("The entry changed since the comparison."));
        else if (isSharedDeletePath(confirmedPath, root, otherRoot))
          report.fail(confirmedPath, tr("The entry or its root is shared by both sides."));
        else
          removeEntry(QFileInfo(confirmedPath), report);
      }
    }
    const QString summary = tr("Deleted %1 files and %2 folders.").arg(report.files).arg(report.folders);
    return OperationResult{ summarize(report.stopped() ? tr("Delete canceled. %1").arg(summary) : summary, report) };
  });
}

void QDirDiffWidget::setInitialExpansion()
{
  const QSignalBlocker blockLeft(viewLeft_);
  const QSignalBlocker blockRight(viewRight_);
  viewLeft_->expandAll();
  viewRight_->expandAll();

  const int rows = proxy_->rowCount();
  for (int row = 0; row < rows; ++row) {
    const auto index = proxy_->index(row, 0);
    if (index.data(IsDirRole).toBool() &&
        index.data(DiffTypeRole).toInt() == static_cast<int>(DiffType::Identical)) {
      viewLeft_->collapse(index);
      viewRight_->collapse(index);
    }
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

void QDirDiffWidget::ComparisonStats::record(const DirDiffNode &entry)
{
  leftFiles += entry.leftKind == DiffEntryKind::File;
  rightFiles += entry.rightKind == DiffEntryKind::File;
  leftFolders += entry.leftKind == DiffEntryKind::Directory;
  rightFolders += entry.rightKind == DiffEntryKind::Directory;

  // Directory markers include changes in descendants, which already have their own entry counts.
  if (entry.leftKind == DiffEntryKind::Directory && entry.rightKind == DiffEntryKind::Directory) return;

  switch (entry.diffType) {
  case DiffType::Identical:
    if (entry.leftKind == DiffEntryKind::File) ++identicalFiles;
    break;
  case DiffType::Modified:
    if (entry.leftKind == entry.rightKind) ++modifiedFiles;
    else ++typeConflicts;
    break;
  case DiffType::Removed: ++leftOnly; break;
  case DiffType::Added: ++rightOnly; break;
  }
}

void QDirDiffWidget::showComparisonStats()
{
  if (!hasComparisonStats()) return;
  ComparisonStats stats;
  stats.scanned = *scanCount_;
  const auto countEntries = [&](const auto &visit, const DirDiffNode &parent) -> void {
    for (const auto &child : parent.children) {
      stats.record(*child);
      visit(visit, *child);
    }
  };
  countEntries(countEntries, *model_->nodeForIndex({}));
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Directory Comparison Stats"));
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  layout->addLayout(form);

  const auto addPath = [&](const QString &label, const QString &root) {
    auto *field = new QLineEdit(QDir::toNativeSeparators(root), &dialog);
    field->setReadOnly(true);
    field->setCursorPosition(0);
    form->addRow(label, field);
  };
  const QLocale locale;
  const auto count = [&](size_t value) { return locale.toString(static_cast<qulonglong>(value)); };
  const auto addCount = [&](const QString &label, const QString &value) {
    form->addRow(label, new QLabel(value, &dialog));
  };
  addPath(tr("Left root"), model_->leftRoot());
  addPath(tr("Right root"), model_->rightRoot());
  addCount(tr("Entries scanned"), count(stats.scanned));
  addCount(tr("Files included (left / right)"), tr("%1 / %2").arg(count(stats.leftFiles), count(stats.rightFiles)));
  addCount(tr("Folders included (left / right)"), tr("%1 / %2").arg(count(stats.leftFolders), count(stats.rightFolders)));
  addCount(tr("Differences"), count(stats.differences()));
  addCount(tr("Identical file pairs"), count(stats.identicalFiles));
  addCount(tr("Modified file pairs"), count(stats.modifiedFiles));
  addCount(tr("Left-only entries"), count(stats.leftOnly));
  addCount(tr("Right-only entries"), count(stats.rightOnly));
  addCount(tr("File/folder conflicts"), count(stats.typeConflicts));

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  buttons->setFocus();
  dialog.resize(460, dialog.sizeHint().height());
  dialog.exec();
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
  if (job_) return;
  const QString a = headerLeft_->currentPath().trimmed();
  const QString b = headerRight_->currentPath().trimmed();
  if (a.isEmpty() || b.isEmpty()) {
    showStatus(tr("Please set both directories before comparing."));
    return;
  }

  startComparison(QDir(a).absolutePath(), QDir(b).absolutePath());
}

void QDirDiffWidget::beginJob(Job kind)
{
  const bool hadStats = hasComparisonStats();
  jobKind_ = kind;
  job_ = std::make_shared<QDirDiffJobState>();
  headerLeft_->setEnabled(false);
  headerRight_->setEnabled(false);
  cancelButton_->setEnabled(true);
  progressRow_->show();
  updateProgress();
  progressTimer_->start();
  if (hadStats) emit comparisonStatsAvailabilityChanged();
}

void QDirDiffWidget::endJob()
{
  job_.reset();
  progressTimer_->stop();
  progressRow_->hide();
  headerLeft_->setEnabled(true);
  headerRight_->setEnabled(true);
  if (hasComparisonStats()) emit comparisonStatsAvailabilityChanged();
}

void QDirDiffWidget::showStatus(const QString &message)
{
  statusText_ = message;
  emit statusMessage(message);
}

void QDirDiffWidget::updateProgress()
{
  if (!job_) return;
  QString text;
  int percent = -1;
  if (job_->stop.stop_requested()) {
    text = tr("Canceling…");
  } else if (jobKind_ == Job::Compare) {
    const auto &progress = job_->comparison;
    switch (progress.phase.load()) {
    case DirectoryDiffPhase::Scanning:
      text = tr("Scanning directories… %1 entries scanned").arg(progress.scanned.load());
      break;
    case DirectoryDiffPhase::Matching:
      text = tr("Matching paths…");
      break;
    case DirectoryDiffPhase::Comparing: {
      const auto total = progress.filesToCompare.load();
      const auto completed = progress.compared.load();
      percent = static_cast<int>(100.0 * completed / total);
      text = tr("Comparing file contents… %1 of %2").arg(completed).arg(total);
      break;
    }
    case DirectoryDiffPhase::Finalizing:
      text = tr("Preparing comparison results…");
      if (progress.filesToCompare.load() > 0) percent = 100;
      break;
    }
  } else {
    const auto progress = job_->fileProgress();
    text = jobKind_ == Job::Copy ? tr("Copying… %1 entries completed").arg(progress.completed)
      : tr("Deleting… %1 entries completed").arg(progress.completed);
    if (!progress.path.isEmpty())
      text += QStringLiteral("  ") + QDir::toNativeSeparators(progress.path);
  }
  progressBar_->setRange(0, percent < 0 ? 0 : 100);
  if (percent >= 0) progressBar_->setValue(percent);
  progressLabel_->setText(progressLabel_->fontMetrics().elidedText(text, Qt::ElideMiddle, progressLabel_->width()));
  progressLabel_->setToolTip(text);
}

void QDirDiffWidget::startFileOperation(Job kind, std::function<OperationResult(QDirDiffJobState&)> operation)
{
  beginJob(kind);
  operationWatcher_->setFuture(QtConcurrent::run([job = job_, operation = std::move(operation)] {
    const std::unique_lock lock(fileMutationMutex, std::try_to_lock);
    if (!lock.owns_lock())
      return OperationResult{ tr("Another file operation is already running."), false };
    try {
      return operation(*job);
    } catch (const std::exception &error) {
      return OperationResult{ tr("File operation failed. %1").arg(QString::fromLocal8Bit(error.what())) };
    }
  }));
  showStatus(kind == Job::Copy ? tr("Copying…") : tr("Deleting…"));
}

void QDirDiffWidget::onFileOperationFinished()
{
  auto future = operationWatcher_->future();
  const auto result = future.isResultReadyAt(0) ? future.takeResult()
    : OperationResult{ tr("File operation failed before producing a result.") };
  if (result.refresh) {
    startComparison(model_->leftRoot(), model_->rightRoot(), result.summary);
  } else {
    if (!result.copied.isEmpty()) {
      proxy_->setDynamicSortFilter(false);
      model_->applyCopies(result.copied, result.leftToRight);
      proxy_->refreshFilter();
      proxy_->setDynamicSortFilter(true);
    }
    endJob();
    showStatus(result.summary);
    emit compareFinished();
  }
}

void QDirDiffWidget::startComparison(const QString &a, const QString &b, const QString &operation)
{
  beginJob(Job::Compare);
  scanCount_.reset();
  operationSummary_ = operation;
  if (operation.isEmpty())
    emit compareStarted();
  showStatus(operation.isEmpty() ? tr("Comparing…") : tr("%1 Refreshing comparison…").arg(operation));
  model_->clear();

  DiffOptions opts;
  opts.includeIdentical = true;
  opts.includeDirectories = true;
  opts.captureFileFingerprints = true;
  for (const auto &pattern : exclusions())
    opts.ignorePatterns.push_back(pattern.toStdString());

  auto future = QtConcurrent::run([a, b, opts = std::move(opts), job = job_] {
    ComparisonResult result{ a, b };
    if (job->stop.stop_requested()) return result;
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
        [&](const FileDiff &entry) { result.files.push_back(entry); }, job->stop.get_token(), &job->comparison);
      result.scanned = job->comparison.scanned.load();
    } catch (const std::filesystem::filesystem_error &error) {
      const QString path = QFileInfo(error.path1()).filePath();
      const QString reason = QString::fromLocal8Bit(error.code().message().c_str());
      result.error = path.isEmpty() ? reason : tr("Cannot scan \"%1\" (%2).").arg(path, reason);
    } catch (const std::exception &error) {
      result.error = QString::fromLocal8Bit(error.what());
    }
    return result;
    });
  watcher_->setFuture(future);
}

void QDirDiffWidget::onDiffFinished()
{
  QString message;
  auto future = watcher_->future();
  if (job_->stop.stop_requested()) {
    message = tr("Comparison canceled.");
  } else if (!future.isResultReadyAt(0)) {
    message = tr("Comparison failed before producing a result.");
  } else {
    const auto result = future.takeResult();
    if (!result.error.isEmpty()) {
      message = tr("Comparison failed. %1").arg(result.error);
    } else {
      model_->setDiffResults(result.files, result.leftRoot, result.rightRoot);
      viewLeft_->header()->resizeSections(QHeaderView::ResizeToContents);
      viewRight_->header()->resizeSections(QHeaderView::ResizeToContents);
      setInitialExpansion();
      scanCount_ = result.scanned;
      const auto differences = std::count_if(result.files.begin(), result.files.end(),
        [](const FileDiff &entry) { return entry.type != DiffType::Identical; });
      message = tr("Done. %1 differences found.").arg(differences);
    }
  }

  if (!operationSummary_.isEmpty())
    message = tr("%1 %2").arg(operationSummary_, message);
  operationSummary_.clear();
  endJob();
  showStatus(message);
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
  , root_(std::make_shared<DirDiffNode>())
{
}

void QDirDiffModel::clear()
{
  beginResetModel();
  root_ = std::make_shared<DirDiffNode>();
  endResetModel();
}

void QDirDiffModel::setDiffResults(const std::vector<FileDiff> &diffs, const QString &dirA, const QString &dirB)
{
  beginResetModel();
  root_ = std::make_shared<DirDiffNode>();
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
    if (!diff.rightRelativePath.empty())
      node->rightName = QFileInfo(diff.rightRelativePath).fileName();
    node->leftFingerprint = diff.leftFingerprint;
    node->rightFingerprint = diff.rightFingerprint;
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

void QDirDiffModel::applyCopies(const CopiedEntries &copied, bool leftToRight)
{
  QSet<const DirDiffNode*> affected;
  for (auto entry = copied.cbegin(); entry != copied.cend(); ++entry) {
    for (auto *node = entry.key(); node != root_.get() && !affected.contains(node); node = node->parent)
      affected.insert(node);
  }

  std::vector<std::pair<QModelIndex, QModelIndex>> changed;
  const auto update = [&](const auto &visit, DirDiffNode *parent, const QModelIndex &parentIndex) -> void {
    int firstChanged = -1;
    int lastChanged = -1;
    for (int row = 0; row < static_cast<int>(parent->children.size()); ++row) {
      auto *node = parent->children[row].get();
      if (!affected.contains(node)) continue;

      auto &destinationKind = leftToRight ? node->rightKind : node->leftKind;
      destinationKind = node->kind(leftToRight);
      if (const auto entry = copied.constFind(node); entry != copied.cend()) {
        node->resolvedByCopy = true;
        if (entry->has_value()) {
          node->leftFingerprint = *entry;
          node->rightFingerprint = *entry;
          node->diffType = DiffType::Identical;
        }
      }

      const auto rowIndex = index(row, 0, parentIndex);
      if (node->isDir()) {
        visit(visit, node, rowIndex);
        const bool different = std::any_of(node->children.begin(), node->children.end(),
          [](const auto &child) { return child->diffType != DiffType::Identical; });
        node->diffType = different ? DiffType::Modified : DiffType::Identical;
      }
      if (firstChanged == -1) firstChanged = row;
      lastChanged = row;
    }
    if (firstChanged != -1)
      changed.emplace_back(index(firstChanged, 0, parentIndex), index(lastChanged, 2, parentIndex));
  };
  update(update, root_.get(), {});
  for (const auto &[first, last] : changed)
    emit dataChanged(first, last);
}

DirDiffNode *QDirDiffModel::nodeForIndex(const QModelIndex &idx) const
{
  if (!idx.isValid()) return root_.get();
  return static_cast<DirDiffNode *>(idx.internalPointer());
}

std::shared_ptr<const DirDiffNode> QDirDiffModel::snapshotForIndex(const QModelIndex &idx) const
{
  // The worker can keep a selected subtree alive even if the tab closes.
  return { root_, nodeForIndex(idx) };
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
      return node->nameOnSide(col == DiffColumn::LeftName);
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
  case ResolvedByCopyRole:
    return node->resolvedByCopy && node->diffType == DiffType::Identical;
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
  if (idx.data(ResolvedByCopyRole).toBool()) return false;
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
