#pragma once
#include <QIcon>
#include <QWidget>
#include <QComboBox>
#include <QToolButton>
#include <QTreeView>
#include <QSplitter>
#include <QFutureWatcher>
#include <QSortFilterProxyModel>

#include <memory>

#include "directory-diff/dirdiff.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QGenericPanelHeader;
class QDirDiffModel;
class QDirDiffProxyModel;
struct QDirDiffJobState;
struct DirDiffNode;

// ─────────────────────────────────────────────────────────────────────────────

class QDirDiffWidget : public QWidget {
  Q_OBJECT
public:
  explicit QDirDiffWidget(QWidget* parent = nullptr);
  ~QDirDiffWidget() override;

  void compareDirs();
  static QStringList exclusions();
  static void setExclusions(const QStringList& patterns);
  QDirDiffProxyModel *proxyModel() const { return proxy_; }
  QString leftPath()  const;
  QString rightPath() const;
  QString statusText() const { return statusText_; }

Q_SIGNALS:
  void statusMessage(const QString& msg);
  void compareStarted();
  void compareFinished();
  void compareFileRequested(const QString &leftFile, const QString &rightFile, bool leftMissing, bool rightMissing);

private Q_SLOTS:
  void onDiffFinished();
  void onFileOperationFinished();

private:
  enum class Job { Compare, Copy, Delete };

  void setupViews();
  void syncExpansion(QTreeView* source, QTreeView* target, const QModelIndex& idx, bool expanded);
  QString relPathForNode(const DirDiffNode* node, bool left) const;
  void copySelection(const QModelIndexList& selection, bool leftToRight);
  void deleteEntry(const QModelIndex& idx, bool leftSide);
  QModelIndexList operationSelection(QTreeView* source) const;
  void startFileOperation(Job kind, std::function<QString(QDirDiffJobState&)> operation);
  void startComparison(const QString& leftRoot, const QString& rightRoot, const QString& operation = {});
  void beginJob(Job kind);
  void endJob();
  void updateProgress();
  void showStatus(const QString& message);
  void expandChangedFolders(const QModelIndex& parent = {});

private:
  struct ComparisonResult {
    QString leftRoot;
    QString rightRoot;
    std::vector<FileDiff> files;
    QString error;
  };

  struct OperationResult {
    QString summary;
    bool refresh = true;
  };

  QGenericPanelHeader * headerLeft_ = nullptr;
  QGenericPanelHeader * headerRight_ = nullptr;
  QTreeView* viewLeft_ = nullptr;
  QTreeView* viewRight_ = nullptr;
  QSplitter* splitter_ = nullptr;
  QDirDiffModel* model_ = nullptr;
  QDirDiffProxyModel *proxy_ = nullptr;

  QFutureWatcher<ComparisonResult>* watcher_ = nullptr;
  QFutureWatcher<OperationResult>* operationWatcher_ = nullptr;
  std::shared_ptr<QDirDiffJobState> job_;
  Job jobKind_ = Job::Compare;
  QWidget* progressRow_ = nullptr;
  QLabel* progressLabel_ = nullptr;
  QProgressBar* progressBar_ = nullptr;
  QPushButton* cancelButton_ = nullptr;
  QTimer* progressTimer_ = nullptr;
  QString operationSummary_;
  QString statusText_;
  bool syncingExpansion_ = false;
  bool syncingSelection_ = false;
};


// ─────────────────────────────────────────────────────────────────────────────

class QGenericPanelHeader : public QWidget {
  Q_OBJECT;
public:
  explicit QGenericPanelHeader(const QString& side, bool dirPanel, QWidget* parent = nullptr);

  QString currentPath() const;
  void setPath(const QString& path);

Q_SIGNALS:
  void pathChanged(const QString& path);
  void returnPressed();

private:
  void saveHistory(const QString& path);
  void loadHistory();

private:
  bool dirPanel_ = false;
  QComboBox* pathCombo_ = nullptr;
  QToolButton* browseBtn_ = nullptr;
};


// ─────────────────────────────────────────────────────────────────────────────

struct DirDiffNode {
  QString name;
  QString rightName;
  DiffType diffType = DiffType::Identical;
  DiffEntryKind leftKind = DiffEntryKind::Missing;
  DiffEntryKind rightKind = DiffEntryKind::Missing;
  std::optional<FileFingerprint> leftFingerprint;
  std::optional<FileFingerprint> rightFingerprint;

  DirDiffNode* parent = nullptr;
  std::vector<std::unique_ptr<DirDiffNode>> children;

  DiffEntryKind kind(bool left) const { return left ? leftKind : rightKind; }
  const QString& nameOnSide(bool left) const { return left || rightName.isEmpty() ? name : rightName; }
  const std::optional<FileFingerprint>& fingerprint(bool left) const
  { return left ? leftFingerprint : rightFingerprint; }
  bool isDir() const;

  DirDiffNode* findOrAddChild(const QString& childName);
};


// ─────────────────────────────────────────────────────────────────────────────

enum class DiffColumn {
  LeftName = 0, // filename shown in left panel
  DiffMark = 1, // ← → ↔ ≠ symbol in the centre column
  RightName = 2, // filename shown in right panel
  Count
};

// Extra roles
enum DirDiffRole {
  DiffTypeRole = Qt::UserRole + 1,   // DiffType as int
  IsDirRole = Qt::UserRole + 2,
  TypeConflictRole = Qt::UserRole + 3,
};


// ─────────────────────────────────────────────────────────────────────────────

class QDirDiffModel : public QAbstractItemModel {
  Q_OBJECT
public:
  explicit QDirDiffModel(QObject* parent = nullptr);

  // Rebuild the whole tree from a diff result.
  void setDiffResults(const std::vector<FileDiff>& diffs, const QString& dirA, const QString& dirB);
  QString leftRoot() const { return dirA_; }
  QString rightRoot() const { return dirB_; }

  void clear();

  QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
  QModelIndex parent(const QModelIndex& child) const override;
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  // nullptr => root
  DirDiffNode* nodeForIndex(const QModelIndex& idx) const;
  std::shared_ptr<const DirDiffNode> snapshotForIndex(const QModelIndex& idx) const;

private:
  void updateDirectories(DirDiffNode* node);

  std::shared_ptr<DirDiffNode> root_;
  QString dirA_;
  QString dirB_;
  mutable QIcon iconFolder_;
  mutable QIcon iconFile_;
};

// ─────────────────────────────────────────────────────────────────────────────

class QDirDiffProxyModel : public QSortFilterProxyModel {
  Q_OBJECT
public:
  explicit QDirDiffProxyModel(QObject *parent = nullptr);

public slots:
  void setShowIdentical(bool show);
  void setShowModified(bool show);
  void setShowAdded(bool show);
  void setShowRemoved(bool show);

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
  bool rowOrAnyChildAccepted(int sourceRow, const QModelIndex &sourceParent) const;
  bool diffTypeAccepted(DiffType t) const;

  bool showIdentical_ = true;
  bool showModified_ = true;
  bool showAdded_ = true;
  bool showRemoved_ = true;
};
