#pragma once

#include <QMainWindow>

class QMenu;
class QAction;
class QToolBar;
class QTabWidget;
class QDirDiffWidget;

class QMainWindowDiff : public QMainWindow {
  Q_OBJECT
public:
  explicit QMainWindowDiff(QWidget* parent = nullptr);

private slots:
  void compareFileRequested(const QString &leftFile, const QString &rightFile);

private:
  void createToolBar();
  void createCentralWidgets();

  QTabWidget* tabWidget()       const;
  QDirDiffWidget* currentDirDiff()  const;

  QToolBar* mainToolBar_ = nullptr;
  QAction* actNewDirDiff_ = nullptr;
  QAction* actNewFileDiff_ = nullptr;
  QAction* actCompare_ = nullptr;
};