#pragma once

#include <QWidget>
#include <QSplitter>
#include <QTableWidget>
#include <QFutureWatcher>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QMenu>
#include <QTextCharFormat>
#include <QSyntaxHighlighter>
#include <QRegularExpression>

#include "directory-diff/filediff.h"

class QGenericPanelHeader;

// -----------------------------------------------------------------------------
// Syntax highlighter for table cells
// -----------------------------------------------------------------------------
class QCellSyntaxHighlighter : public QSyntaxHighlighter {
  Q_OBJECT
public:
  explicit QCellSyntaxHighlighter(QTextDocument *parent = nullptr);
  void setLanguage(const QString &filePath);

protected:
  void highlightBlock(const QString &text) override;

private:
  struct HighlightingRule {
    QRegularExpression pattern;
    QTextCharFormat format;
  };

  QVector<HighlightingRule> highlightingRules_;
  QTextCharFormat keywordFormat_;
  QTextCharFormat stringFormat_;
  QTextCharFormat commentFormat_;
  QTextCharFormat numberFormat_;

  void initCppRules();
};

// -----------------------------------------------------------------------------
// Main file diff widget - Table-based aligned view
// -----------------------------------------------------------------------------
class QFileDiffWidget : public QWidget {
  Q_OBJECT
public:
  explicit QFileDiffWidget(QWidget *parent = nullptr);
  ~QFileDiffWidget();

  void setFiles(const QString &leftPath, const QString &rightPath);
  void compareFiles();
  void copySelection(bool leftToRight);
  void copyAll(bool leftToRight);
  void goToNextDifference();
  void goToPreviousDifference();
  void zoomIn();
  void zoomOut();
  void setSyntaxHighlighting(bool enabled);

  QString leftPath() const { return leftPath_; }
  QString rightPath() const { return rightPath_; }
  QString leftFile() const { return leftPath(); }
  QString rightFile() const { return rightPath(); }

signals:
  void statusMessage(const QString &msg);
  void compareStarted();
  void compareFinished();

private slots:
  void onDiffFinished();
  void onCellDoubleClicked(int row, int column);
  void onCurrentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn);

private:
  void setupUI();
  void setupContextMenu();
  void buildAlignedTable(const DiffResult &diff, const QStringList &leftLines, const QStringList &rightLines);
  void highlightRow(int row, EditType type);
  void updateStatusBar();
  void saveSettings();
  void loadSettings();
  int findNextDiff(int startRow);
  int findPrevDiff(int startRow);

  QList<int> getSelectedRows() const;

private:
  QGenericPanelHeader *headerLeft_ = nullptr;
  QGenericPanelHeader *headerRight_ = nullptr;
  QTableWidget *tableWidget_;
  QWidget *toolbar_;
  QLabel *statusLabel_;
  QPushButton *nextDiffBtn_;
  QPushButton *prevDiffBtn_;
  QComboBox *zoomCombo_;
  QAction *syntaxHighlightAction_;

  QString leftPath_;
  QString rightPath_;

  QFutureWatcher<DiffResult> *watcher_;
  QVector<int> diffRows_;  // Rows that contain differences

  // Settings
  int fontSize_;
  bool syntaxHighlighting_;
};
