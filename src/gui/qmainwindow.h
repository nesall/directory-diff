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

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void compareFileRequested(const QString &leftFile, const QString &rightFile,
    bool leftMissing = false, bool rightMissing = false);

private:
  void createToolBar();
  void createCentralWidgets();
  void showSettings();
  void updateStatsAction();

  QTabWidget* tabWidget()       const;
  QDirDiffWidget* currentDirDiff()  const;

  QToolBar* mainToolBar_ = nullptr;
  QAction* actNewDirDiff_ = nullptr;
  QAction* actNewFileDiff_ = nullptr;
  QAction* actCompare_ = nullptr;
  QAction* actStats_ = nullptr;
};
