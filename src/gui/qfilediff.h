#pragma once

#include <QFutureWatcher>
#include <QWidget>
#include <array>
#include <optional>

#include "directory-diff/filediff.h"

class QAction;
class QTableView;
class QGenericPanelHeader;
class QFileDiffModel;

class QFileDiffWidget : public QWidget {
  Q_OBJECT
public:
  explicit QFileDiffWidget(QWidget* parent = nullptr);

  void setFiles(const QString& leftPath, const QString& rightPath,
    bool leftMissing = false, bool rightMissing = false);
  void compareFiles();
  bool confirmDiscardChanges();

  QString leftFile() const { return comparedPaths_[0]; }
  QString rightFile() const { return comparedPaths_[1]; }
  QString statusText() const { return statusText_; }

signals:
  void statusMessage(const QString& message);
  void compareStarted();
  void compareFinished();

private:
  struct FileSide {
    QString path;
    TextFile document;
    std::optional<std::string> savedBytes;
    bool dirty = false;
  };

  struct Comparison {
    std::optional<std::array<FileSide, 2>> sides;
    DiffResult diff;
    QString error;
  };

  struct BlockEdit {
    int side;
    int row;
    size_t offset;
    size_t inserted;
    std::vector<Line> removed;
    bool previousBom;
  };

  void onDiffFinished();
  void navigate(int direction);
  void updateControls();
  void reportCurrentChange();
  void showStatus(const QString& message);
  void selectChange(int row);
  void copyText(int side);
  void applyChange(int side);
  void undoChange();
  void replaceLines(int side, size_t offset, size_t count, std::vector<Line> replacement);
  void updateDiff(int row);
  void saveFile(int side);
  void setBusy(bool busy);

  QGenericPanelHeader* headers_[2]{};
  QTableView* view_;
  QFileDiffModel* model_;
  QAction* previous_;
  QAction* next_;
  QAction* apply_[2]{};
  QAction* undo_;
  QAction* save_[2]{};
  QFutureWatcher<Comparison>* watcher_;

  std::array<QString, 2> comparedPaths_;
  std::array<QString, 2> knownMissingPaths_;
  std::array<FileSide, 2> sides_;
  std::vector<BlockEdit> edits_;
  QString statusText_;
  int preferredRow_ = 0;
  bool busy_ = false;
  bool hasResult_ = false;
};
