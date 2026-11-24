#include "qfilediff.h"
#include "filemutation.h"
#include "qdirdiff.h"

#include <QAbstractTableModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QSettings>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

namespace {
  class FileDiffToolBar : public QToolBar {
  public:
    explicit FileDiffToolBar(QWidget* parent) : QToolBar(parent)
    {
      connect(this, &QToolBar::iconSizeChanged, this, &FileDiffToolBar::refreshArrowIcons);
    }

    QAction* addArrowAction(QStyle::StandardPixmap arrow, const QString& text, auto handler)
    {
      auto* action = addAction(text, this, std::move(handler));
      arrows_.emplace_back(action, arrow);
      setArrowIcon(action, arrow);
      return action;
    }

  protected:
    void changeEvent(QEvent* event) override
    {
      QToolBar::changeEvent(event);
      if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        refreshArrowIcons();
    }

  private:
    void refreshArrowIcons()
    {
      for (const auto& [action, arrow] : arrows_)
        setArrowIcon(action, arrow);
    }

    void setArrowIcon(QAction* action, QStyle::StandardPixmap arrow)
    {
      const QIcon original = style()->standardIcon(arrow, nullptr, this);
      QIcon tinted;
      for (qreal scale : { 1.0, 2.0 }) {
        const QPixmap mask = original.pixmap(iconSize(), scale);
        if (mask.isNull()) continue;
        for (auto mode : { QIcon::Normal, QIcon::Disabled }) {
          const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
          QPixmap pixmap = mask;
          {
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(QRectF(QPointF(), pixmap.deviceIndependentSize()),
              palette().color(group, QPalette::ButtonText));
          }
          tinted.addPixmap(pixmap, mode);
        }
      }
      action->setIcon(tinted.isNull() ? original : tinted);
    }

    std::vector<std::pair<QAction*, QStyle::StandardPixmap>> arrows_;
  };

  QString displayLine(const Line& line)
  {
    constexpr qsizetype tabWidth = 2;
    const QString text = QString::fromStdString(line.text);
    auto tab = text.indexOf(u'\t');
    if (tab < 0) return text;

    QString expanded;
    expanded.reserve(text.size());
    qsizetype start = 0;
    for (; tab >= 0; tab = text.indexOf(u'\t', start)) {
      expanded += QStringView(text).sliced(start, tab - start);
      expanded += QString(tabWidth - expanded.size() % tabWidth, u' ');
      start = tab + 1;
    }
    expanded += QStringView(text).sliced(start);
    return expanded;
  }
}

class QFileDiffModel : public QAbstractTableModel {
public:
  enum Column { LeftNumber, LeftText, RightNumber, RightText, ColumnCount };

  struct Block {
    Hunk hunk;
    int firstRow;
    std::array<size_t, 2> offsets;
  };

  explicit QFileDiffModel(QObject* parent) : QAbstractTableModel(parent) {}

  void setDiff(DiffResult diff, const std::array<QString, 2>& paths,
    const std::array<bool, 2>& missing, const std::array<bool, 2>& modified = {})
  {
    beginResetModel();
    paths_ = paths;
    missing_ = missing;
    modified_ = modified;
    rows_ = 0;
    blocks_.clear();
    changes_.clear();
    std::array<size_t, 2> offsets{};
    for (auto& hunk : diff) {
      if (hunk.type != EditType::Equal)
        changes_.push_back(rows_);
      blocks_.push_back({ std::move(hunk), rows_, offsets });
      const auto& stored = blocks_.back().hunk;
      offsets[0] += stored.linesA.size();
      offsets[1] += stored.linesB.size();
      rows_ += static_cast<int>(std::max(stored.linesA.size(), stored.linesB.size()));
    }
    endResetModel();
  }

  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : rows_; }
  int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }
  const std::vector<int>& changes() const { return changes_; }

  void setFileState(int side, bool missing, bool modified)
  {
    if (missing_[side] == missing && modified_[side] == modified) return;
    missing_[side] = missing;
    modified_[side] = modified;
    const int column = side == 0 ? LeftText : RightText;
    emit headerDataChanged(Qt::Horizontal, column, column);
  }

  const Block* blockAt(int row) const
  {
    if (row < 0 || row >= rows_) return nullptr;
    const auto next = std::upper_bound(blocks_.begin(), blocks_.end(), row,
      [](int value, const Block& block) { return value < block.firstRow; });
    return &*std::prev(next);
  }

  const Line* lineAt(int row, int side) const
  {
    const auto* block = blockAt(row);
    if (!block) return nullptr;
    return lineAt(*block, row, side);
  }

  QVariant data(const QModelIndex& index, int role) const override
  {
    const auto* block = blockAt(index.row());
    if (!index.isValid() || !block) return {};

    const bool number = index.column() == LeftNumber || index.column() == RightNumber;
    const int side = index.column() < RightNumber ? 0 : 1;
    const auto* line = lineAt(*block, index.row(), side);

    if (role == Qt::TextAlignmentRole)
      return int(number ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter);
    if (role == Qt::DisplayRole && line) {
      if (number) return line->number;
      return displayLine(*line);
    }
    if (role == Qt::ToolTipRole && line) {
      const QString text = displayLine(*line).toHtmlEscaped();
      const QString ending = line->ending == LineEnding::None ? tr("No newline at end of file") : QString();
      return QStringLiteral("<pre>%1</pre>%2").arg(text, ending);
    }
    if (role == Qt::BackgroundRole && block->hunk.type != EditType::Equal) {
      const auto palette = QApplication::palette();
      if (!line) return palette.color(QPalette::AlternateBase);
      const QColor base = palette.color(QPalette::Base);
      const QColor tint = side == 0 ? QColor(230, 77, 69) : QColor(50, 174, 103);
      return QColor((base.red() * 3 + tint.red()) / 4,
        (base.green() * 3 + tint.green()) / 4, (base.blue() * 3 + tint.blue()) / 4);
    }
    return {};
  }

  QVariant headerData(int section, Qt::Orientation orientation, int role) const override
  {
    if (orientation != Qt::Horizontal || section < 0 || section >= ColumnCount) return {};
    if (section == LeftNumber || section == RightNumber)
      return role == Qt::DisplayRole ? QVariant(QStringLiteral("#")) : QVariant();
    const int side = section == LeftText ? 0 : 1;
    if (role == Qt::ToolTipRole) return paths_[side];
    if (role != Qt::DisplayRole) return {};
    if (paths_[side].isEmpty()) return side == 0 ? tr("Left") : tr("Right");
    QString name = QFileInfo(paths_[side]).fileName();
    if (modified_[side])
      name += u'*';
    return missing_[side] ? tr("%1 (missing)").arg(name) : name;
  }

private:
  static const Line* lineAt(const Block& block, int row, int side)
  {
    const auto& lines = side == 0 ? block.hunk.linesA : block.hunk.linesB;
    const auto offset = static_cast<size_t>(row - block.firstRow);
    return offset < lines.size() ? &lines[offset] : nullptr;
  }

  std::vector<Block> blocks_;
  std::vector<int> changes_;
  std::array<QString, 2> paths_;
  std::array<bool, 2> missing_{};
  std::array<bool, 2> modified_{};
  int rows_ = 0;
};

QFileDiffWidget::QFileDiffWidget(QWidget* parent)
  : QWidget(parent),
    view_(new QTableView(this)),
    model_(new QFileDiffModel(this)),
    watcher_(new QFutureWatcher<Comparison>(this))
{
  auto* layout = new QVBoxLayout(this);
  auto* paths = new QHBoxLayout;
  headers_[0] = new QGenericPanelHeader(tr("Left:"), false, this);
  headers_[1] = new QGenericPanelHeader(tr("Right:"), false, this);
  for (auto* header : headers_) {
    paths->addWidget(header, 1);
    connect(header, &QGenericPanelHeader::returnPressed, this, &QFileDiffWidget::compareFiles);
  }
  layout->addLayout(paths);

  auto* toolbar = new FileDiffToolBar(this);
  toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
#ifdef Q_OS_MACOS
  toolbar->setIconSize(QSize(16, 16));
#endif
  previous_ = toolbar->addArrowAction(QStyle::SP_ArrowUp, tr("Previous Change"), [this] { navigate(-1); });
  next_ = toolbar->addArrowAction(QStyle::SP_ArrowDown, tr("Next Change"), [this] { navigate(1); });
  toolbar->addSeparator();
  apply_[0] = toolbar->addArrowAction(QStyle::SP_ArrowLeft, tr("Apply to Left"), [this] { applyChange(0); });
  apply_[1] = toolbar->addArrowAction(QStyle::SP_ArrowRight, tr("Apply to Right"), [this] { applyChange(1); });
  undo_ = toolbar->addAction(QIcon(":/undo"), tr("Undo"), this, &QFileDiffWidget::undoChange);
  toolbar->addSeparator();
  save_[0] = toolbar->addAction(QIcon(":/save-left"), tr("Save Left"), this, [this] { saveFile(0); });
  save_[1] = toolbar->addAction(QIcon(":/save-right"), tr("Save Right"), this, [this] { saveFile(1); });
  layout->addWidget(toolbar);

  view_->setModel(model_);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view_->setWordWrap(false);
  view_->setShowGrid(false);
  view_->verticalHeader()->setVisible(false);
  view_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  font.setPointSize(QSettings().value("FileDiff/fontSize", 10).toInt());
  view_->setFont(font);
  view_->verticalHeader()->setDefaultSectionSize(view_->fontMetrics().height() + 6);
  auto* columns = view_->horizontalHeader();
  columns->setSectionResizeMode(QFileDiffModel::LeftNumber, QHeaderView::Fixed);
  columns->setSectionResizeMode(QFileDiffModel::RightNumber, QHeaderView::Fixed);
  columns->setSectionResizeMode(QFileDiffModel::LeftText, QHeaderView::Stretch);
  columns->setSectionResizeMode(QFileDiffModel::RightText, QHeaderView::Stretch);
  const int lineNumberWidth = view_->fontMetrics().horizontalAdvance(QStringLiteral("0000000")) + 8;
  columns->resizeSection(QFileDiffModel::LeftNumber, lineNumberWidth);
  columns->resizeSection(QFileDiffModel::RightNumber, lineNumberWidth);

  view_->setContextMenuPolicy(Qt::ActionsContextMenu);
  auto* copyLeft = new QAction(tr("Copy Left Text"), view_);
  auto* copyRight = new QAction(tr("Copy Right Text"), view_);
  connect(copyLeft, &QAction::triggered, this, [this] { copyText(0); });
  connect(copyRight, &QAction::triggered, this, [this] { copyText(1); });
  view_->addActions({ copyLeft, copyRight });
  connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
    updateControls();
    reportCurrentChange();
  });
  connect(watcher_, &QFutureWatcher<Comparison>::finished, this, &QFileDiffWidget::onDiffFinished);

  layout->addWidget(view_, 1);
  updateControls();
}

void QFileDiffWidget::setFiles(const QString& leftPath, const QString& rightPath,
  bool leftMissing, bool rightMissing)
{
  headers_[0]->setPath(leftPath);
  headers_[1]->setPath(rightPath);
  knownMissingPaths_ = {
    leftMissing ? QFileInfo(leftPath).absoluteFilePath() : QString(),
    rightMissing ? QFileInfo(rightPath).absoluteFilePath() : QString()
  };
}

void QFileDiffWidget::compareFiles()
{
  if (busy_) return;
  const std::array paths{ headers_[0]->currentPath(), headers_[1]->currentPath() };
  if (paths[0].isEmpty() || paths[1].isEmpty()) {
    showStatus(tr("Choose both files to compare."));
    return;
  }
  if (!confirmDiscardChanges()) return;
  comparedPaths_ = paths;
  preferredRow_ = 0;
  hasResult_ = false;
  sides_ = {};
  edits_.clear();
  setBusy(true);
  model_->setDiff({}, paths, {});
  emit compareStarted();
  showStatus(tr("Comparing files…"));

  watcher_->setFuture(QtConcurrent::run([paths, missingPaths = knownMissingPaths_] {
    Comparison result;
    auto& sides = result.sides.emplace();
    for (int side = 0; side < 2; ++side) {
      auto& file = sides[side];
      const QFileInfo info(paths[side]);
      file.path = info.absoluteFilePath();
      try {
        if (file.path != missingPaths[side] || info.exists() || info.isSymLink()) {
          file.document = loadTextFile(info.filesystemAbsoluteFilePath());
          file.savedBytes = textFileBytes(file.document);
        }
      } catch (const std::filesystem::filesystem_error& error) {
        result.error = tr("Cannot compare %1. %2").arg(file.path, QString::fromLocal8Bit(error.code().message().c_str()));
        return result;
      } catch (const std::exception& error) {
        result.error = tr("Cannot compare %1. %2").arg(file.path, QString::fromLocal8Bit(error.what()));
        return result;
      }
    }
    result.diff = myersDiff(sides[0].document.lines, sides[1].document.lines);
    return result;
  }));
}

void QFileDiffWidget::onDiffFinished()
{
  auto future = watcher_->future();
  QString error;
  if (future.isResultReadyAt(0)) {
    auto result = future.takeResult();
    error = result.error;
    if (error.isEmpty()) {
      if (result.sides)
        sides_ = std::move(*result.sides);
      model_->setDiff(std::move(result.diff), { sides_[0].path, sides_[1].path },
        { !sides_[0].savedBytes, !sides_[1].savedBytes }, { sides_[0].dirty, sides_[1].dirty });
      hasResult_ = true;
    }
  } else {
    error = tr("The comparison failed before producing a result.");
  }
  if (!error.isEmpty()) {
    hasResult_ = false;
    model_->setDiff({}, comparedPaths_, {}, { sides_[0].dirty, sides_[1].dirty });
  }
  const auto& changes = model_->changes();
  if (hasResult_ && !changes.empty()) {
    const auto nearest = std::lower_bound(changes.begin(), changes.end(), preferredRow_);
    selectChange(nearest == changes.end() ? changes.back() : *nearest);
  }
  setBusy(false);
  if (error.isEmpty())
    reportCurrentChange();
  else
    showStatus(error);
  emit compareFinished();
}

void QFileDiffWidget::navigate(int direction)
{
  const auto& changes = model_->changes();
  if (busy_ || changes.empty()) return;
  const int row = view_->currentIndex().row();
  if (direction > 0) {
    const auto next = std::upper_bound(changes.begin(), changes.end(), row);
    selectChange(next == changes.end() ? changes.front() : *next);
  } else {
    const auto* block = model_->blockAt(row);
    const int start = block && block->hunk.type != EditType::Equal ? block->firstRow : row;
    const auto previous = std::lower_bound(changes.begin(), changes.end(), start);
    selectChange(previous == changes.begin() ? changes.back() : *std::prev(previous));
  }
}

void QFileDiffWidget::selectChange(int row)
{
  const QModelIndex index = model_->index(row, QFileDiffModel::LeftText);
  view_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  view_->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

void QFileDiffWidget::updateControls()
{
  const auto& changes = model_->changes();
  const auto* block = model_->blockAt(view_->currentIndex().row());
  const bool selectedChange = block && block->hunk.type != EditType::Equal;
  previous_->setEnabled(!busy_ && !changes.empty());
  next_->setEnabled(!busy_ && !changes.empty());
  for (int side = 0; side < 2; ++side) {
    apply_[side]->setEnabled(!busy_ && hasResult_ && selectedChange);
    save_[side]->setEnabled(!busy_ && sides_[side].dirty);
    save_[side]->setToolTip(sides_[side].path.isEmpty() ? save_[side]->text()
      : tr("%1\n%2").arg(save_[side]->text(), sides_[side].path));
  }
  undo_->setEnabled(!busy_ && !edits_.empty());
}

void QFileDiffWidget::reportCurrentChange()
{
  if (busy_ || !hasResult_) return;
  const auto& changes = model_->changes();
  const auto* block = model_->blockAt(view_->currentIndex().row());
  if (changes.empty()) {
    showStatus(tr("Files are identical."));
  } else if (block && block->hunk.type != EditType::Equal) {
    const auto position = std::lower_bound(changes.begin(), changes.end(), block->firstRow);
    showStatus(tr("Change %1 of %2").arg(std::distance(changes.begin(), position) + 1).arg(changes.size()));
  } else {
    showStatus(tr("%1 changes").arg(changes.size()));
  }
}

void QFileDiffWidget::showStatus(const QString& message)
{
  statusText_ = message;
  emit statusMessage(statusText_);
}

void QFileDiffWidget::copyText(int side)
{
  auto rows = view_->selectionModel()->selectedRows();
  std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) {
    return a.row() < b.row();
  });
  QStringList text;
  for (const auto& row : rows) {
    if (const auto* line = model_->lineAt(row.row(), side))
      text.append(QString::fromStdString(line->text));
  }
  if (!text.isEmpty())
    QApplication::clipboard()->setText(text.join('\n'));
}

bool QFileDiffWidget::confirmDiscardChanges()
{
  QStringList paths;
  for (const auto& side : sides_) {
    if (side.dirty)
      paths.append(side.path);
  }
  if (paths.isEmpty()) return true;
  return QMessageBox::warning(this, tr("Unsaved Changes"),
    tr("Discard the unsaved changes to these files?\n\n%1").arg(paths.join('\n')),
    QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

void QFileDiffWidget::setBusy(bool busy)
{
  busy_ = busy;
  view_->setEnabled(!busy);
  for (auto* header : headers_)
    header->setEnabled(!busy);
  updateControls();
}

void QFileDiffWidget::applyChange(int side)
{
  if (busy_ || !hasResult_) return;
  const auto* block = model_->blockAt(view_->currentIndex().row());
  if (!block || block->hunk.type == EditType::Equal) return;

  auto& target = sides_[side];
  const auto& source = sides_[1 - side];
  const auto& removed = side == 0 ? block->hunk.linesA : block->hunk.linesB;
  auto replacement = side == 0 ? block->hunk.linesB : block->hunk.linesA;
  const auto ending = std::find_if(target.document.lines.begin(), target.document.lines.end(),
    [](const Line& line) { return line.ending != LineEnding::None; });
  if (ending != target.document.lines.end()) {
    for (auto& line : replacement) {
      if (line.ending != LineEnding::None)
        line.ending = ending->ending;
    }
  }

  edits_.push_back({ side, block->firstRow, block->offsets[side], replacement.size(), removed, target.document.utf8Bom });
  if (!target.savedBytes && target.document.lines.empty())
    target.document.utf8Bom = source.document.utf8Bom;
  replaceLines(side, block->offsets[side], removed.size(), std::move(replacement));
  updateDiff(block->firstRow);
}

void QFileDiffWidget::undoChange()
{
  if (busy_ || edits_.empty()) return;
  auto edit = std::move(edits_.back());
  edits_.pop_back();
  sides_[edit.side].document.utf8Bom = edit.previousBom;
  replaceLines(edit.side, edit.offset, edit.inserted, std::move(edit.removed));
  updateDiff(edit.row);
}

void QFileDiffWidget::replaceLines(int side, size_t offset, size_t count, std::vector<Line> replacement)
{
  auto& target = sides_[side];
  auto& lines = target.document.lines;
  const auto start = lines.begin() + offset;
  const auto position = lines.erase(start, start + count);
  lines.insert(position, std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
  for (size_t i = offset; i < lines.size(); ++i)
    lines[i].number = static_cast<int>(i) + 1;
  target.dirty = target.savedBytes ? textFileBytes(target.document) != *target.savedBytes
    : !lines.empty() || target.document.utf8Bom;
  model_->setFileState(side, !target.savedBytes, target.dirty);
}

void QFileDiffWidget::updateDiff(int row)
{
  preferredRow_ = row;
  setBusy(true);
  showStatus(tr("Updating comparison…"));
  watcher_->setFuture(QtConcurrent::run([left = sides_[0].document.lines, right = sides_[1].document.lines] {
    Comparison result;
    result.diff = myersDiff(left, right);
    return result;
  }));
}

void QFileDiffWidget::saveFile(int side)
{
  auto& target = sides_[side];
  if (busy_ || !target.dirty) return;

  const auto reportFailure = [&](const QString& detail) {
    showStatus(tr("Cannot save %1. %2").arg(target.path, detail));
  };
  std::unique_lock lock(fileMutationMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    reportFailure(tr("A directory copy or delete is running. Try again when it finishes."));
    return;
  }
  const QFileInfo info(target.path);
  if (target.savedBytes) {
    QFile original(target.path);
    if (!original.open(QIODevice::ReadOnly)) {
      reportFailure(original.errorString());
      return;
    }
    const auto expected = QByteArrayView(target.savedBytes->data(), static_cast<qsizetype>(target.savedBytes->size()));
    if (original.size() != expected.size() || original.readAll() != expected || original.error() != QFileDevice::NoError) {
      reportFailure(tr("The file changed on disk."));
      return;
    }
  } else {
    if (info.exists() || info.isSymLink()) {
      reportFailure(tr("A file has appeared at this path."));
      return;
    }
    if (!QDir().mkpath(info.absolutePath())) {
      reportFailure(tr("The parent directory could not be created."));
      return;
    }
  }

  auto bytes = textFileBytes(target.document);
  QSaveFile file(target.path);
  const auto size = static_cast<qint64>(bytes.size());
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes.data(), size) != size || !file.commit()) {
    reportFailure(file.errorString());
    return;
  }
  lock.unlock();
  target.savedBytes = std::move(bytes);
  target.dirty = false;
  model_->setFileState(side, false, false);
  updateControls();
  showStatus(tr("Saved %1").arg(target.path));
}
