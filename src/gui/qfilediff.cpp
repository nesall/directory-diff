#include "qfilediff.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QFont>
#include <QSettings>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QPalette>
#include <QToolBar>

#include "qdirdiff.h"
#include "qtutils.h"

// -----------------------------------------------------------------------------
// QCellSyntaxHighlighter Implementation
// -----------------------------------------------------------------------------

QCellSyntaxHighlighter::QCellSyntaxHighlighter(QTextDocument *parent)
  : QSyntaxHighlighter(parent)
{
  keywordFormat_.setForeground(QColor(0, 0, 255));
  keywordFormat_.setFontWeight(QFont::Bold);
  stringFormat_.setForeground(QColor(160, 32, 240));
  commentFormat_.setForeground(QColor(0, 128, 0));
  numberFormat_.setForeground(QColor(139, 69, 19));
}

void QCellSyntaxHighlighter::setLanguage(const QString &filePath) {
  QFileInfo info(filePath);
  QString ext = info.suffix().toLower();

  if (ext == "cpp" || ext == "cxx" || ext == "cc" || ext == "c" || ext == "h" || ext == "hpp") {
    initCppRules();
  } else {
    highlightingRules_.clear();
  }
}

void QCellSyntaxHighlighter::initCppRules() {
  highlightingRules_.clear();

  QStringList keywords = { "auto", "break", "case", "catch", "char", "class", "const", "continue",
      "default", "delete", "do", "double", "else", "enum", "false", "float", "for", "friend",
      "goto", "if", "int", "long", "namespace", "new", "operator", "private", "protected",
      "public", "return", "short", "signed", "sizeof", "static", "struct", "switch", "template",
      "this", "throw", "true", "try", "typedef", "typename", "union", "unsigned", "using",
      "virtual", "void", "volatile", "while" };

  for (const QString &keyword : keywords) {
    HighlightingRule rule;
    rule.pattern = QRegularExpression("\\b" + keyword + "\\b");
    rule.format = keywordFormat_;
    highlightingRules_.append(rule);
  }

  HighlightingRule stringRule;
  stringRule.pattern = QRegularExpression("\".*\"");
  stringRule.format = stringFormat_;
  highlightingRules_.append(stringRule);

  HighlightingRule commentRule;
  commentRule.pattern = QRegularExpression("//[^\n]*");
  commentRule.format = commentFormat_;
  highlightingRules_.append(commentRule);

  HighlightingRule numberRule;
  numberRule.pattern = QRegularExpression("\\b[0-9]+\\b");
  numberRule.format = numberFormat_;
  highlightingRules_.append(numberRule);
}

void QCellSyntaxHighlighter::highlightBlock(const QString &text) {
  for (const HighlightingRule &rule : highlightingRules_) {
    QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
    while (it.hasNext()) {
      QRegularExpressionMatch match = it.next();
      setFormat(match.capturedStart(), match.capturedLength(), rule.format);
    }
  }
}

// -----------------------------------------------------------------------------
// QFileDiffWidget Implementation
// -----------------------------------------------------------------------------

QFileDiffWidget::QFileDiffWidget(QWidget *parent)
  : QWidget(parent)
  , tableWidget_(new QTableWidget(this))
  , toolbar_(nullptr)
  , statusLabel_(nullptr)
  , nextDiffBtn_(nullptr)
  , prevDiffBtn_(nullptr)
  , zoomCombo_(nullptr)
  , watcher_(new QFutureWatcher<DiffResult>(this))
  , fontSize_(10)
  , syntaxHighlighting_(true)
{
  setupUI();
  setupContextMenu();
  loadSettings();

  connect(watcher_, &QFutureWatcher<DiffResult>::finished, this, &QFileDiffWidget::onDiffFinished);
}

QFileDiffWidget::~QFileDiffWidget() {
  saveSettings();
}

void QFileDiffWidget::setupUI()
{
  headerLeft_ = new QGenericPanelHeader(tr("Left:"), true, this);
  headerRight_ = new QGenericPanelHeader(tr("Right:"), true, this);

  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  auto headerBar = new QWidget(this);
  auto hbox = new QHBoxLayout(headerBar);
  hbox->setContentsMargins(4, 4, 4, 4);
  hbox->addWidget(headerLeft_, 1);
  hbox->addWidget(headerRight_, 1);
  mainLayout->addWidget(headerBar);

  // Toolbar
  //toolbar_ = new QWidget(this);
  //auto *toolbarLayout = new QHBoxLayout(toolbar_);
  //toolbarLayout->setContentsMargins(4, 2, 4, 2);

  //prevDiffBtn_ = new QPushButton(tr("← Previous Diff"), this);
  //nextDiffBtn_ = new QPushButton(tr("Next Diff →"), this);

  //zoomCombo_ = new QComboBox(this);
  //for (int size : {8, 9, 10, 11, 12, 14, 16, 18, 20, 24}) {
  //  zoomCombo_->addItem(QString::number(size), size);
  //}
  //zoomCombo_->setCurrentIndex(zoomCombo_->findData(fontSize_));

  //statusLabel_ = new QLabel(tr("Ready"), this);
  //statusLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  //toolbarLayout->addWidget(prevDiffBtn_);
  //toolbarLayout->addWidget(nextDiffBtn_);
  //toolbarLayout->addWidget(new QVertLine);
  //toolbarLayout->addWidget(new QLabel(tr("Zoom:")));
  //toolbarLayout->addWidget(zoomCombo_);
  //toolbarLayout->addStretch();
  //toolbarLayout->addWidget(statusLabel_);

  //mainLayout->addWidget(toolbar_);

  // Table widget for aligned diff view
  tableWidget_->setColumnCount(3);
  tableWidget_->setHorizontalHeaderLabels({ tr(""), tr("Left"), tr("Right") });
  tableWidget_->horizontalHeader()->setStretchLastSection(false);
  tableWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  tableWidget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  tableWidget_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  tableWidget_->verticalHeader()->setVisible(false);
  tableWidget_->setColumnWidth(0, 60);  // Diff marker column
  //tableWidget_->setAlternatingRowColors(true);
  tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableWidget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tableWidget_->setFont(QFont("Consolas", fontSize_));

  mainLayout->addWidget(tableWidget_, 1);

  // Connect signals
  connect(nextDiffBtn_, &QPushButton::clicked, this, &QFileDiffWidget::goToNextDifference);
  connect(prevDiffBtn_, &QPushButton::clicked, this, &QFileDiffWidget::goToPreviousDifference);
  connect(zoomCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), [this]() {
    fontSize_ = zoomCombo_->currentData().toInt();
    QFont font = tableWidget_->font();
    font.setPointSize(fontSize_);
    tableWidget_->setFont(font);
    saveSettings();
    });

  connect(tableWidget_, &QTableWidget::cellDoubleClicked, this, &QFileDiffWidget::onCellDoubleClicked);
  connect(tableWidget_, &QTableWidget::currentCellChanged, this, &QFileDiffWidget::onCurrentCellChanged);

  auto fCompare = [this]() {
    leftPath_ = headerLeft_->currentPath();
    rightPath_ = headerRight_->currentPath();
    compareFiles();
    };

  connect(headerLeft_, &QGenericPanelHeader::returnPressed, this, fCompare);
  connect(headerRight_, &QGenericPanelHeader::returnPressed, this, fCompare);
}

void QFileDiffWidget::setupContextMenu() {
  tableWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tableWidget_, &QWidget::customContextMenuRequested, [this](const QPoint &pos) {
    QModelIndex index = tableWidget_->indexAt(pos);
    if (!index.isValid()) return;

    QMenu menu;
    auto *copyLeftAction = menu.addAction(tr("Copy Left Cell"));
    auto *copyRightAction = menu.addAction(tr("Copy Right Cell"));
    menu.addSeparator();
    auto *copyRowLeftAction = menu.addAction(tr("Copy Left Row"));
    auto *copyRowRightAction = menu.addAction(tr("Copy Right Row"));
    menu.addSeparator();
    auto *copyToRightAction = menu.addAction(tr("Copy Left → Right"));
    auto *copyToLeftAction = menu.addAction(tr("Copy Right → Left"));

    connect(copyLeftAction, &QAction::triggered, [this, index]() {
      QTableWidgetItem *item = tableWidget_->item(index.row(), 1);
      if (item) QApplication::clipboard()->setText(item->text());
      });

    connect(copyRightAction, &QAction::triggered, [this, index]() {
      QTableWidgetItem *item = tableWidget_->item(index.row(), 2);
      if (item) QApplication::clipboard()->setText(item->text());
      });

    connect(copyRowLeftAction, &QAction::triggered, [this, index]() {
      // Copy all left cells from selected rows
      QStringList texts;
      for (int row : getSelectedRows()) {
        QTableWidgetItem *item = tableWidget_->item(row, 1);
        if (item && !item->text().isEmpty())
          texts.append(item->text());
      }
      if (!texts.isEmpty())
        QApplication::clipboard()->setText(texts.join("\n"));
      });

    connect(copyRowRightAction, &QAction::triggered, [this, index]() {
      QStringList texts;
      for (int row : getSelectedRows()) {
        QTableWidgetItem *item = tableWidget_->item(row, 2);
        if (item && !item->text().isEmpty())
          texts.append(item->text());
      }
      if (!texts.isEmpty())
        QApplication::clipboard()->setText(texts.join("\n"));
      });

    connect(copyToRightAction, &QAction::triggered, [this]() { copySelection(true); });
    connect(copyToLeftAction, &QAction::triggered, [this]() { copySelection(false); });

    menu.exec(tableWidget_->viewport()->mapToGlobal(pos));
    });
}

QList<int> QFileDiffWidget::getSelectedRows() const {
  QList<int> rows;
  for (const QModelIndex &idx : tableWidget_->selectionModel()->selectedRows()) {
    rows.append(idx.row());
  }
  return rows;
}

void QFileDiffWidget::setFiles(const QString &leftPath, const QString &rightPath)
{
  leftPath_ = leftPath;
  rightPath_ = rightPath;
  headerLeft_->setPath(leftPath);
  headerRight_->setPath(rightPath);

  // Update headers
  QFileInfo leftInfo(leftPath_), rightInfo(rightPath_);
  if (auto *item = tableWidget_->horizontalHeaderItem(1)) item->setText(leftInfo.fileName());
  if (auto *item = tableWidget_->horizontalHeaderItem(2)) item->setText(rightInfo.fileName());
}

void QFileDiffWidget::compareFiles() {
  if (leftPath_.isEmpty() || rightPath_.isEmpty()) {
    emit statusMessage(tr("Please set both files before comparing."));
    return;
  }

  emit compareStarted();
  emit statusMessage(tr("Comparing files..."));

  auto future = QtConcurrent::run([leftPath = leftPath_, rightPath = rightPath_]() -> DiffResult {
    std::vector<Line> leftLines = loadLines(leftPath.toStdString());
    std::vector<Line> rightLines = loadLines(rightPath.toStdString());
    return myersDiff(leftLines, rightLines);
    });

  watcher_->setFuture(future);
}

void QFileDiffWidget::onDiffFinished()
{
  DiffResult diffResult = watcher_->result();

  // Load raw lines
  QStringList leftLines, rightLines;
  QFile leftFile(leftPath_), rightFile(rightPath_);

  if (leftFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QString content = QString::fromUtf8(leftFile.readAll());
    leftLines = content.split('\n');
  }
  if (rightFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QString content = QString::fromUtf8(rightFile.readAll());
    rightLines = content.split('\n');
  }

  buildAlignedTable(diffResult, leftLines, rightLines);
  updateStatusBar();

  emit compareFinished();
  emit statusMessage(tr("Comparison complete. %1 differences found.").arg(diffRows_.size()));
}

void QFileDiffWidget::buildAlignedTable(const DiffResult &diff, const QStringList &leftLines, const QStringList &rightLines)
{
  QVector<QPair<QString, QString>> alignedRows;  // left, right
  QVector<EditType> rowTypes;

  int leftIdx = 0;
  int rightIdx = 0;

  for (const auto &hunk : diff) {
    switch (hunk.type) {
    case EditType::Equal:
      for (auto &line : hunk.linesA) {
        alignedRows.append({ QString::fromStdString(line.text), QString::fromStdString(line.text) });
        rowTypes.append(EditType::Equal);
      }
      break;
    case EditType::Delete:
      for (auto &line : hunk.linesA) {
        alignedRows.append({ QString::fromStdString(line.text), "" });
        rowTypes.append(EditType::Delete);
      }
      break;
    case EditType::Insert:
      for (auto &line : hunk.linesB) {
        alignedRows.append({ "", QString::fromStdString(line.text) });
        rowTypes.append(EditType::Insert);
      }
      break;
    case EditType::Replace: {
      size_t maxSize = std::max(hunk.linesA.size(), hunk.linesB.size());
      for (size_t i = 0; i < maxSize; ++i) {
        QString l = i < hunk.linesA.size() ? QString::fromStdString(hunk.linesA[i].text) : "";
        QString r = i < hunk.linesB.size() ? QString::fromStdString(hunk.linesB[i].text) : "";
        alignedRows.append({ l, r });
        rowTypes.append(EditType::Replace);
      }
      break;
    }
    }
  }

  // Setup table
  diffRows_.clear();
  tableWidget_->setRowCount(alignedRows.size());

  for (int row = 0; row < alignedRows.size(); ++row) {
    // Diff marker column
    QString marker;
    switch (rowTypes[row]) {
    case EditType::Delete: marker = "-"; diffRows_.append(row); break;
    case EditType::Insert: marker = "+"; diffRows_.append(row); break;
    case EditType::Replace: marker = "≠"; diffRows_.append(row); break;
    default: marker = " ";
    }

    QTableWidgetItem *markerItem = new QTableWidgetItem(marker);
    markerItem->setTextAlignment(Qt::AlignCenter);
    tableWidget_->setItem(row, 0, markerItem);

    // Left column
    QTableWidgetItem *leftItem = new QTableWidgetItem(alignedRows[row].first);
    leftItem->setToolTip(QString("Line %1").arg(row + 1));
    tableWidget_->setItem(row, 1, leftItem);

    // Right column
    QTableWidgetItem *rightItem = new QTableWidgetItem(alignedRows[row].second);
    rightItem->setToolTip(QString("Line %1").arg(row + 1));
    tableWidget_->setItem(row, 2, rightItem);

    // Apply syntax highlighting if enabled
    if (syntaxHighlighting_) {
      if (!alignedRows[row].first.isEmpty()) {
        //auto *highlighter = new QCellSyntaxHighlighter(leftItem->document());
        //highlighter->setLanguage(leftPath_);
      }
      if (!alignedRows[row].second.isEmpty()) {
        //auto *highlighter = new QCellSyntaxHighlighter(rightItem->document());
        //highlighter->setLanguage(rightPath_);
      }
    }

    // Color the row based on diff type
    highlightRow(row, rowTypes[row]);
  }

  // Resize rows to content
  tableWidget_->resizeRowsToContents();
}

void QFileDiffWidget::highlightRow(int row, EditType type) {
  QColor bgColor;
  QColor fgColor;

  switch (type) {
  case EditType::Delete:
    bgColor = QColor(255, 220, 220);  // Light red
    fgColor = QColor(139, 0, 0);      // Dark red
    break;
  case EditType::Insert:
    bgColor = QColor(220, 255, 220);  // Light green
    fgColor = QColor(0, 100, 0);      // Dark green
    break;
  case EditType::Replace:
    bgColor = QColor(255, 245, 220);  // Light orange
    fgColor = QColor(139, 69, 19);    // Brown
    break;
  default:
    return;
  }

  for (int col = 0; col < 3; ++col) {
    QTableWidgetItem *item = tableWidget_->item(row, col);
    if (item) {
      item->setBackground(bgColor);
      item->setForeground(fgColor);
    }
  }
}

void QFileDiffWidget::copySelection(bool leftToRight) {
  QList<int> selectedRows = getSelectedRows();
  if (selectedRows.isEmpty()) return;

  // Build content to copy
  QStringList lines;
  for (int row : selectedRows) {
    QTableWidgetItem *item = tableWidget_->item(row, leftToRight ? 1 : 2);
    if (item && !item->text().isEmpty()) {
      lines.append(item->text());
    }
  }

  if (!lines.isEmpty()) {
    QApplication::clipboard()->setText(lines.join("\n"));
    emit statusMessage(tr("Copied %1 lines from %2")
      .arg(lines.size())
      .arg(leftToRight ? tr("left") : tr("right")));
  }
}

void QFileDiffWidget::copyAll(bool leftToRight) {
  QStringList lines;
  for (int row = 0; row < tableWidget_->rowCount(); ++row) {
    QTableWidgetItem *item = tableWidget_->item(row, leftToRight ? 1 : 2);
    if (item && !item->text().isEmpty()) {
      lines.append(item->text());
    }
  }

  if (!lines.isEmpty()) {
    QApplication::clipboard()->setText(lines.join("\n"));
    emit statusMessage(tr("Copied all %1 lines from %2")
      .arg(lines.size())
      .arg(leftToRight ? tr("left") : tr("right")));
  }
}

int QFileDiffWidget::findNextDiff(int startRow)
{
  for (int row : diffRows_) {
    if (row > startRow) return row;
  }
  return diffRows_.isEmpty() ? -1 : diffRows_[0]; // wrap
}

int QFileDiffWidget::findPrevDiff(int startRow)
{
  for (int i = diffRows_.size() - 1; i >= 0; --i) {
    if (diffRows_[i] < startRow) return diffRows_[i];
  }
  return diffRows_.isEmpty() ? -1 : diffRows_.last(); // wrap
}

void QFileDiffWidget::goToNextDifference() {
  if (diffRows_.isEmpty()) {
    emit statusMessage(tr("No differences found"));
    return;
  }

  int currentRow = tableWidget_->currentRow();
  int nextRow = findNextDiff(currentRow);

  if (nextRow >= 0) {
    tableWidget_->selectRow(nextRow);
    tableWidget_->scrollToItem(tableWidget_->item(nextRow, 0), QAbstractItemView::PositionAtCenter);

    int diffIndex = diffRows_.indexOf(nextRow) + 1;
    emit statusMessage(tr("Difference %1 of %2").arg(diffIndex).arg(diffRows_.size()));
  }
}

void QFileDiffWidget::goToPreviousDifference() {
  if (diffRows_.isEmpty()) {
    emit statusMessage(tr("No differences found"));
    return;
  }

  int currentRow = tableWidget_->currentRow();
  int prevRow = findPrevDiff(currentRow);

  if (prevRow >= 0) {
    tableWidget_->selectRow(prevRow);
    tableWidget_->scrollToItem(tableWidget_->item(prevRow, 0), QAbstractItemView::PositionAtCenter);

    int diffIndex = diffRows_.indexOf(prevRow) + 1;
    emit statusMessage(tr("Difference %1 of %2").arg(diffIndex).arg(diffRows_.size()));
  }
}

void QFileDiffWidget::zoomIn() {
  fontSize_ = std::min(24, fontSize_ + 1);
  QFont font = tableWidget_->font();
  font.setPointSize(fontSize_);
  tableWidget_->setFont(font);
  tableWidget_->resizeRowsToContents();
  zoomCombo_->setCurrentIndex(zoomCombo_->findData(fontSize_));
  saveSettings();
}

void QFileDiffWidget::zoomOut() {
  fontSize_ = std::max(8, fontSize_ - 1);
  QFont font = tableWidget_->font();
  font.setPointSize(fontSize_);
  tableWidget_->setFont(font);
  tableWidget_->resizeRowsToContents();
  zoomCombo_->setCurrentIndex(zoomCombo_->findData(fontSize_));
  saveSettings();
}

void QFileDiffWidget::setSyntaxHighlighting(bool enabled) {
  syntaxHighlighting_ = enabled;
  saveSettings();
  compareFiles();  // Refresh with new setting
}

void QFileDiffWidget::onCellDoubleClicked(int row, int column) {
  if (column == 1) {
    QTableWidgetItem *item = tableWidget_->item(row, 1);
    if (item && !item->text().isEmpty()) {
      QMessageBox::information(this, tr("Line Content"),
        tr("Left line %1:\n\n%2").arg(row + 1).arg(item->text()));
    }
  } else if (column == 2) {
    QTableWidgetItem *item = tableWidget_->item(row, 2);
    if (item && !item->text().isEmpty()) {
      QMessageBox::information(this, tr("Line Content"),
        tr("Right line %1:\n\n%2").arg(row + 1).arg(item->text()));
    }
  }
}

void QFileDiffWidget::onCurrentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn)
{
  Q_UNUSED(currentColumn);
  Q_UNUSED(previousRow);
  Q_UNUSED(previousColumn);

  //if (currentRow >= 0 && diffRows_.contains(currentRow)) {
  //  int diffIndex = diffRows_.indexOf(currentRow) + 1;
  //  statusLabel_->setText(tr("Row %1: Difference %2 of %3")
  //    .arg(currentRow + 1)
  //    .arg(diffIndex)
  //    .arg(diffRows_.size()));
  //} else if (currentRow >= 0) {
  //  statusLabel_->setText(tr("Row %1: Identical").arg(currentRow + 1));
  //}
}

void QFileDiffWidget::updateStatusBar()
{
  //statusLabel_->setText(tr("Total: %1 rows, %2 differences")
  //  .arg(tableWidget_->rowCount())
  //  .arg(diffRows_.size()));
}

void QFileDiffWidget::saveSettings() {
  QSettings settings;
  settings.beginGroup("FileDiff");
  settings.setValue("fontSize", fontSize_);
  settings.setValue("syntaxHighlighting", syntaxHighlighting_);
  settings.endGroup();
}

void QFileDiffWidget::loadSettings() {
  QSettings settings;
  settings.beginGroup("FileDiff");
  fontSize_ = settings.value("fontSize", 10).toInt();
  syntaxHighlighting_ = settings.value("syntaxHighlighting", true).toBool();
  settings.endGroup();
}

