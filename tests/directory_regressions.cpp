#include "qdirdiff.h"
#include "qfilediff.h"
#include "qmainwindow.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSemaphore>
#include <QSettings>
#include <QShortcut>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>

#include <iostream>
#include <stdexcept>

namespace {
  void check(bool condition, const char* message)
  {
    if (!condition) throw std::runtime_error(message);
  }

  void writeFile(const QDir& root, const QString& name, const QByteArray& bytes)
  {
    const QString path = root.filePath(name);
    check(QDir().mkpath(QFileInfo(path).absolutePath()), "Could not create fixture directory");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Could not write fixture file");
  }

  QByteArray readFile(const QDir& root, const QString& name)
  {
    QFile file(root.filePath(name));
    check(file.open(QIODevice::ReadOnly), "Could not read fixture file");
    return file.readAll();
  }

  QModelIndex rowNamed(QAbstractItemModel* model, const QString& name,
    int column = 0, const QModelIndex& parent = {})
  {
    for (int row = 0; row < model->rowCount(parent); ++row) {
      const auto index = model->index(row, column, parent);
      if (index.data().toString() == name)
        return index;
    }
    return {};
  }

  void waitForComparison(QDirDiffWidget& widget, auto start)
  {
    QEventLoop loop;
    bool completed = false;
    QObject::connect(&widget, &QDirDiffWidget::compareFinished, &loop, [&] {
      completed = true;
      loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    start();
    if (!completed) loop.exec();
    check(completed, "Directory comparison did not finish");
  }

  void compare(QDirDiffWidget& widget, const QDir& left, const QDir& right)
  {
    for (auto* header : widget.findChildren<QGenericPanelHeader*>()) {
      const auto* label = header->findChild<QLabel*>();
      header->setPath(label->text() == "Left:" ? left.path() : right.path());
    }
    waitForComparison(widget, [&] { widget.compareDirs(); });
  }

  QTreeView* selectLeftRow(QDirDiffWidget& widget, const QString& name)
  {
    for (auto* view : widget.findChildren<QTreeView*>()) {
      if (view->isColumnHidden(0)) continue;
      const auto row = rowNamed(view->model(), name);
      check(row.isValid(), "Source row is missing");
      view->setFocus();
      view->selectionModel()->setCurrentIndex(row, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      return view;
    }
    throw std::runtime_error("Left tree is missing");
  }

  void startCopyLeft(QDirDiffWidget& widget)
  {
    for (auto* shortcut : widget.findChildren<QShortcut*>()) {
      if (shortcut->key() != QKeySequence(Qt::CTRL | Qt::Key_W)) continue;
      check(QMetaObject::invokeMethod(shortcut, "activated"), "Copy action unavailable");
      return;
    }
    throw std::runtime_error("Copy shortcut is missing");
  }

  void copyLeftRow(QDirDiffWidget& widget, const QString& name)
  {
    selectLeftRow(widget, name);
    waitForComparison(widget, [&] { startCopyLeft(widget); });
  }

  // Hold the only Qt worker so the test can cancel through the UI before disk I/O starts.
  class OccupiedPool {
  public:
    OccupiedPool()
    {
      pool_->setMaxThreadCount(1);
      QSemaphore entered;
      pool_->start([gate = gate_, &entered] {
        entered.release();
        gate->acquire();
      });
      entered.acquire();
    }

    ~OccupiedPool()
    {
      gate_->release();
      pool_->setMaxThreadCount(previousLimit_);
    }

  private:
    QThreadPool* pool_ = QThreadPool::globalInstance();
    int previousLimit_ = pool_->maxThreadCount();
    std::shared_ptr<QSemaphore> gate_ = std::make_shared<QSemaphore>();
  };

  void cancelJob(QDirDiffWidget& widget)
  {
    for (auto* button : widget.findChildren<QPushButton*>()) {
      if (button->text() != "Cancel") continue;
      check(button->isVisibleTo(&widget) && button->isEnabled(), "Cancel is unavailable while a job is active");
      button->click();
      check(!button->isEnabled(), "Cancel should be disabled after it is requested");
      return;
    }
    throw std::runtime_error("Cancel button is missing");
  }

  void startDeleteLeft(QDirDiffWidget& widget, const QString& name)
  {
    auto* view = selectLeftRow(widget, name);
    const QPoint position = view->visualRect(view->currentIndex()).center();
    bool confirmed = false;
    QTimer::singleShot(0, view, [&] {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!menu) return;
      menu->close();
      for (auto* action : menu->actions()) {
        if (action->text() != "Delete") continue;
        QTimer::singleShot(0, view, [&] {
          auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
          if (!prompt) return;
          confirmed = true;
          prompt->button(QMessageBox::Yes)->click();
        });
        action->trigger();
        return;
      }
    });
    check(QMetaObject::invokeMethod(view, "customContextMenuRequested", position), "Delete menu unavailable");
    check(confirmed, "Delete confirmation unavailable");
  }

  void backgroundJobs(const QDir& left, const QDir& right)
  {
    writeFile(left, "batch/file.txt", "source");
    writeFile(left, "batch/excluded.tmp", "excluded");
    writeFile(right, "batch/file.txt", "destination");
    QDirDiffWidget::setExclusions({ "*.tmp" });
    QDirDiffWidget widget;
    widget.resize(900, 500);
    widget.show();
    compare(widget, left, right);

    waitForComparison(widget, [&] {
      OccupiedPool occupied;
      widget.compareDirs();
      cancelJob(widget);
    });
    check(widget.statusText() == "Comparison canceled.", "Comparison cancellation was not reported");
    check(widget.proxyModel()->rowCount() == 0, "Canceled comparison must discard partial results");
    waitForComparison(widget, [&] { widget.compareDirs(); });

    selectLeftRow(widget, "batch");
    waitForComparison(widget, [&] {
      OccupiedPool occupied;
      startCopyLeft(widget);
      check(widget.statusText() == "Copying…", "Copy should return to the GUI while queued");
      cancelJob(widget);
    });
    check(readFile(right, "batch/file.txt") == "destination", "Canceled copy overwrote a file");
    check(!right.exists("batch/excluded.tmp"), "Canceled copy created a file");
    check(widget.statusText().contains("Copy canceled.") && widget.statusText().contains("differences found"),
      "Canceled copy must report its outcome and refresh the comparison");

    waitForComparison(widget, [&] {
      OccupiedPool occupied;
      startDeleteLeft(widget, "batch");
      check(widget.statusText() == "Deleting…", "Delete should return to the GUI while queued");
      cancelJob(widget);
    });
    check(left.exists("batch/file.txt") && left.exists("batch/excluded.tmp"), "Canceled delete removed entries");
    check(widget.statusText().contains("Delete canceled.") && widget.statusText().contains("differences found"),
      "Canceled delete must report its outcome and refresh the comparison");

    waitForComparison(widget, [&] { startDeleteLeft(widget, "batch"); });
    check(!left.exists("batch") && right.exists("batch/file.txt"), "Delete should remove only the confirmed folder");
    check(widget.statusText().contains("Deleted 2 files"), "Delete should count excluded descendants");
  }

  void comparisonProgress(const QDir& left, const QDir& right)
  {
    writeFile(left, "folder/identical.txt", "same");
    writeFile(right, "folder/identical.txt", "same");
    writeFile(left, "folder/left-only.txt", "different");
    DiffOptions options;
    options.includeDirectories = true;
    options.includeIdentical = true;
    options.captureFileFingerprints = true;

    DirectoryDiffProgress progress;
    std::stop_source stop;
    size_t delivered = 0;
    const auto result = diffDirectories(left.filesystemPath(), right.filesystemPath(), options,
      [&](const FileDiff&) { ++delivered; stop.request_stop(); }, stop.get_token(), &progress);
    check(!result && delivered == 1, "Cancellation should stop delivery and withhold the final count");
    check(progress.scanned == 5 && progress.filesToCompare == 2 && progress.compared == 2 &&
      progress.phase == DirectoryDiffPhase::Finalizing, "Comparison progress counts are incomplete");
    check(!fingerprintFile(QFileInfo(left.filePath("folder/identical.txt")).filesystemFilePath(), stop.get_token()),
      "Fingerprinting should honor cancellation");
  }

  void folderExpansion(const QDir& left, const QDir& right)
  {
    for (const auto& root : { left, right }) {
      writeFile(root, "matching/nested/file.txt", "same");
      writeFile(root, "mixed/matching/file.txt", "same");
    }
    writeFile(left, "mixed/changed/nested/file.txt", "left");
    writeFile(right, "mixed/changed/nested/file.txt", "rght");
    writeFile(left, "mixed/left-only/file.txt", "left");
    writeFile(right, "mixed/right-only/file.txt", "right");
    writeFile(left, "mixed/conflict", "file");
    writeFile(right, "mixed/conflict/child.txt", "folder");

    QDirDiffWidget widget;
    compare(widget, left, right);
    auto* model = widget.proxyModel();
    const auto views = widget.findChildren<QTreeView*>();
    check(views.size() == 2, "Expected two directory panes");
    const auto expandedInBoth = [&](const QModelIndex& index, bool expected, const char* message) {
      check(index.isValid(), "Expected folder row is missing");
      for (auto* view : views)
        check(view->isExpanded(index) == expected, message);
    };

    const auto matching = rowNamed(model, "matching");
    const auto mixed = rowNamed(model, "mixed");
    const auto changed = rowNamed(model, "changed", 0, mixed);
    expandedInBoth(matching, false, "A matching tree should start collapsed");
    expandedInBoth(rowNamed(model, "matching", 0, mixed), false, "A matching branch should start collapsed");
    expandedInBoth(mixed, true, "An ancestor of changed entries should start expanded");
    expandedInBoth(changed, true, "A branch with a nested edit should start expanded");
    expandedInBoth(rowNamed(model, "nested", 0, changed), true, "A nested edit should be visible");
    expandedInBoth(rowNamed(model, "left-only", 0, mixed), true, "A left-only branch should start expanded");
    expandedInBoth(rowNamed(model, "right-only", 2, mixed).siblingAtColumn(0), true,
      "A right-only branch should start expanded");
    expandedInBoth(rowNamed(model, "conflict", 0, mixed), true, "A file/folder conflict should start expanded");

    views.back()->expand(matching);
    expandedInBoth(matching, true, "Manually opening a matching folder should open both panes");
    views.front()->collapse(matching);
    expandedInBoth(matching, false, "Manually closing a matching folder should close both panes");

    writeFile(right, "matching/nested/file.txt", "edit");
    waitForComparison(widget, [&] { widget.compareDirs(); });
    expandedInBoth(rowNamed(model, "matching"), true, "A formerly matching folder should open after a new change");
  }

  void fileTabWidth()
  {
    const QString longPath = QStringLiteral("C:/project/") + QString(170, 'W') + ".txt";
    QSettings().setValue("FileDiffHistory", QStringList{ longPath });

    QMainWindowDiff window;
    window.resize(750, 500);
    window.show();
    QApplication::processEvents();
    const int originalWidth = window.width();

    QAction* newFileDiff = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
      if (action->text() == "File Diff") {
        newFileDiff = action;
        break;
      }
    }
    check(newFileDiff, "New File Diff action is missing");
    newFileDiff->trigger();
    QApplication::processEvents();

    auto* tabs = window.findChild<QTabWidget*>();
    auto* file = tabs ? qobject_cast<QFileDiffWidget*>(tabs->currentWidget()) : nullptr;
    check(file, "File Diff did not open");
    check(window.width() == originalWidth, "A long path in File Diff history widened the window");

    const auto combos = file->findChildren<QComboBox*>();
    check(combos.size() == 2, "File Diff path pickers are missing");
    for (auto* combo : combos)
      check(combo->findText(longPath) >= 0, "File Diff history lost the long path");
    file->setFiles(longPath, longPath);
    window.resize(540, window.height());
    QApplication::processEvents();
    check(window.width() == 540, "File Diff prevented shrinking the window");
  }

  void staleCopy(const QDir& left, const QDir& right)
  {
    writeFile(left, "batch/changed.txt", "source");
    writeFile(right, "batch/changed.txt", "old");
    writeFile(left, "batch/appeared.txt", "source");
    writeFile(left, "batch/removed.txt", "source");
    writeFile(right, "batch/removed.txt", "destination to remove");
    writeFile(left, "batch/safe.txt", "copy me");
    writeFile(right, "batch/safe.txt", "old destination");
    writeFile(left, "batch/excluded.tmp", "excluded source");
    writeFile(right, "batch/excluded.tmp", "excluded destination");
    QDirDiffWidget::setExclusions({ "*.tmp" });
    QDirDiffWidget widget;
    QString status;
    QObject::connect(&widget, &QDirDiffWidget::statusMessage, &widget, [&](const QString& message) { status = message; });
    compare(widget, left, right);

    // Same size and timestamp as the compared file. Only the fingerprint detects the edit.
    const auto changedPath = QFileInfo(right.filePath("batch/changed.txt")).filesystemFilePath();
    const auto originalTime = std::filesystem::last_write_time(changedPath);
    writeFile(right, "batch/changed.txt", "new");
    std::filesystem::last_write_time(changedPath, originalTime);
    writeFile(right, "batch/appeared.txt", "newly created");
    check(QFile::remove(right.filePath("batch/removed.txt")), "Could not remove fixture destination");

    copyLeftRow(widget, "batch");
    check(readFile(right, "batch/changed.txt") == "new", "Copy replaced a destination edited after comparison");
    check(readFile(right, "batch/appeared.txt") == "newly created", "Copy replaced a new destination");
    check(!right.exists("batch/removed.txt"), "Copy recreated a destination deleted after comparison");
    check(readFile(right, "batch/excluded.tmp") == "excluded destination", "Copy overwrote an unverified destination");
    check(readFile(right, "batch/safe.txt") == "copy me", "Safe files in the batch should still copy");
    check(status.contains("Failed entries: 4"), "The batch should report its skipped entries");

    copyLeftRow(widget, "batch");
    check(readFile(right, "batch/changed.txt") == "source", "Copy after refresh should use the new fingerprint");
  }

  void caseAliases(const QDir& left, const QDir& right)
  {
    writeFile(left, "Docs/Readme.txt", "source");
    writeFile(right, "docs/README.txt", "destination");
    writeFile(left, "Docs/Same.txt", "identical");
    writeFile(right, "docs/SAME.txt", "identical");
    writeFile(right, "docs/Extra.txt", "right only");
    QDirDiffWidget::setExclusions({});

    QDirDiffWidget widget;
    compare(widget, left, right);
    auto* model = widget.proxyModel();
    const auto docs = rowNamed(model, "Docs");
    check(docs.isValid(), "Left folder is missing");
#ifdef Q_OS_WIN
    if (left.exists("docs/README.txt") && right.exists("Docs/Readme.txt")) {
      check(model->rowCount() == 1 && docs.siblingAtColumn(2).data().toString() == "docs",
        "Case aliases should share one folder row with two spellings");
      const auto readme = rowNamed(model, "Readme.txt", 0, docs);
      check(readme.isValid() && readme.siblingAtColumn(2).data().toString() == "README.txt",
        "Case aliases should retain both actual file names");
      const auto same = rowNamed(model, "Same.txt", 0, docs);
      check(same.isValid() && same.data(DiffTypeRole).toInt() == static_cast<int>(DiffType::Identical),
        "Identical files with casing differences should compare equal");
      check(model->rowCount(docs) == 3 && rowNamed(model, "Extra.txt", 2, docs).isValid(),
        "Right-only children should stay under the paired folder");
      copyLeftRow(widget, "Docs");
      check(readFile(right, "docs/README.txt") == "source", "Copy missed the paired destination");
      check(QDir(right.filePath("docs")).entryList(QDir::Files).contains("README.txt"),
        "Copy changed the existing destination spelling");
      return;
    }
#endif
    check(model->rowCount() == 2, "Case-sensitive names should remain separate");
  }
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  try {
    check(argc == 2, "Specify a regression");
    QTemporaryDir fixture;
    check(fixture.isValid(), "Could not create temporary fixture");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, fixture.path());
    QCoreApplication::setOrganizationName("DirectoryDiffTests");
    QCoreApplication::setApplicationName("DirectoryRegressions");
    const QDir root(fixture.path());
    const QDir left(root.filePath("left")), right(root.filePath("right"));

    const QString test = QString::fromLocal8Bit(argv[1]);
    if (test == "stale-copy")
      staleCopy(left, right);
    else if (test == "case-aliases")
      caseAliases(left, right);
    else if (test == "background-jobs")
      backgroundJobs(left, right);
    else if (test == "comparison-progress")
      comparisonProgress(left, right);
    else if (test == "folder-expansion")
      folderExpansion(left, right);
    else if (test == "file-tab-width")
      fileTabWidth();
    else
      throw std::runtime_error("Unknown regression");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
