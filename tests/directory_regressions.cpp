#include "qdirdiff.h"
#include "qfilediff.h"
#include "qmainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QMap>
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
#include <QToolBar>

#include <iostream>
#include <stdexcept>

namespace {
  void check(bool condition, const char* message)
  {
    if (!condition) throw std::runtime_error(message);
  }

  QAction* actionNamed(QObject& owner, const QString& name)
  {
    for (auto* action : owner.findChildren<QAction*>()) {
      if (action->text() == name) return action;
    }
    return nullptr;
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

  void startCopy(QDirDiffWidget& widget, bool leftToRight = true)
  {
    const auto key = leftToRight ? Qt::Key_W : Qt::Key_Q;
    for (auto* shortcut : widget.findChildren<QShortcut*>()) {
      if (shortcut->key() != QKeySequence(Qt::CTRL | key)) continue;
      check(QMetaObject::invokeMethod(shortcut, "activated"), "Copy action unavailable");
      return;
    }
    throw std::runtime_error("Copy shortcut is missing");
  }

  void copyLeftRow(QDirDiffWidget& widget, const QString& name)
  {
    selectLeftRow(widget, name);
    waitForComparison(widget, [&] { startCopy(widget); });
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
      startCopy(widget);
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
    expandedInBoth(rowNamed(model, "matching", 0, mixed), true, "Nested matching folders should start expanded");
    expandedInBoth(mixed, true, "An ancestor of changed entries should start expanded");
    expandedInBoth(changed, true, "A branch with a nested edit should start expanded");
    expandedInBoth(rowNamed(model, "nested", 0, changed), true, "A nested edit should be visible");
    expandedInBoth(rowNamed(model, "left-only", 0, mixed), true, "A left-only branch should start expanded");
    expandedInBoth(rowNamed(model, "right-only", 2, mixed).siblingAtColumn(0), true,
      "A right-only branch should start expanded");
    expandedInBoth(rowNamed(model, "conflict", 0, mixed), true, "A file/folder conflict should start expanded");

    views.back()->expand(matching);
    expandedInBoth(matching, true, "Manually opening a matching folder should open both panes");
    expandedInBoth(rowNamed(model, "nested", 0, matching), true, "Opening a matching root folder should show its contents");
    views.front()->collapse(matching);
    expandedInBoth(matching, false, "Manually closing a matching folder should close both panes");

    writeFile(right, "matching/nested/file.txt", "edit");
    waitForComparison(widget, [&] { widget.compareDirs(); });
    expandedInBoth(rowNamed(model, "matching"), true, "A formerly matching folder should open after a new change");
  }

  void clipboardActions(const QDir& left, const QDir& right)
  {
    const QString filename = QStringLiteral("日本語 file.txt");
    const QString relative = QStringLiteral("folder/") + filename;
    writeFile(left, relative, "left");
    writeFile(right, relative, "right");

    QDirDiffWidget widget;
    widget.resize(900, 500);
    widget.show();
    compare(widget, left, right);
    QApplication::processEvents();

    for (auto* view : widget.findChildren<QTreeView*>()) {
      const bool rightSide = view->isColumnHidden(0);
      const int nameColumn = rightSide ? 2 : 0;
      const auto folder = rowNamed(view->model(), "folder", nameColumn);
      const auto file = rowNamed(view->model(), filename, nameColumn, folder.siblingAtColumn(0));
      const auto copy = [&](const QModelIndex& index, const QString& name, const QString& expected) {
        const QRect rect = view->visualRect(index);
        check(rect.isValid(), "Clipboard test entry is missing or hidden");
        QApplication::clipboard()->clear();
        QTimer::singleShot(0, view, [&] {
          auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!menu) return;
          menu->close();
          if (auto* action = actionNamed(*menu, name)) action->trigger();
        });
        check(QMetaObject::invokeMethod(view, "customContextMenuRequested", rect.center()),
          "Directory context menu unavailable");
        check(QApplication::clipboard()->text() == expected, "Context menu copied the wrong name or path");
      };
      copy(folder, "Copy Name", "folder");
      copy(file, "Copy Name", filename);
      copy(file, "Copy Relative Path", QDir::toNativeSeparators(relative));
      const auto& root = rightSide ? right : left;
      copy(file, "Copy Full Path", QDir::toNativeSeparators(root.filePath(relative)));
    }
  }

  void comparisonStats(const QDir& left, const QDir& right)
  {
    for (const auto& root : { left, right }) writeFile(root, "folder/identical.txt", "same");
    writeFile(left, "folder/changed.txt", "left");
    writeFile(right, "folder/changed.txt", "right");
    writeFile(left, "folder/left-only.txt", "left only");
    writeFile(right, "folder/right-only.txt", "right only");
    writeFile(left, "folder/excluded.tmp", "excluded");
    writeFile(left, "conflict", "file");
    writeFile(right, "conflict/child.txt", "folder");
    QDirDiffWidget::setExclusions({ "*.tmp" });

    QMainWindowDiff window;
    window.show();
    auto* stats = actionNamed(window, "Stats");
    auto* fileDiff = actionNamed(window, "File Diff");
    auto* tabs = window.findChild<QTabWidget*>();
    auto* directory = tabs ? qobject_cast<QDirDiffWidget*>(tabs->currentWidget()) : nullptr;
    check(stats && fileDiff && directory, "Directory Stats controls are missing");
    check(!stats->icon().pixmap(24, 24).isNull(), "Stats icon could not be rendered");
    check(!stats->isEnabled(), "Stats should be disabled before comparing");
    compare(*directory, left, right);
    check(stats->isEnabled(), "Stats should be enabled after a successful comparison");
    directory->proxyModel()->setShowIdentical(false);

    QMap<QString, QString> values;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (!dialog) return;
      if (auto* form = dialog->findChild<QFormLayout*>()) {
        for (int row = 0; row < form->rowCount(); ++row) {
          auto* label = qobject_cast<QLabel*>(form->itemAt(row, QFormLayout::LabelRole)->widget());
          auto* field = qobject_cast<QLabel*>(form->itemAt(row, QFormLayout::FieldRole)->widget());
          if (label && field) values.insert(label->text(), field->text());
        }
      }
      dialog->accept();
    });
    stats->trigger();
    check(values.value("Entries scanned") == "12", "Stats should include visited entries from both roots");
    check(values.value("Files included (left / right)") == "4 / 4", "Stats should omit excluded files");
    check(values.value("Folders included (left / right)") == "1 / 2", "Stats should count folders per root");
    check(values.value("Identical file pairs") == "1", "Stats should count identical files hidden by the filter");
    check(values.value("Differences") == "5" && values.value("Modified file pairs") == "1" &&
      values.value("Left-only entries") == "1" && values.value("Right-only entries") == "2" &&
      values.value("File/folder conflicts") == "1", "Stats difference breakdown is incorrect");

    fileDiff->trigger();
    check(!stats->isEnabled(), "Stats should be disabled on File Diff tabs");
    tabs->setCurrentWidget(directory);
    check(stats->isEnabled(), "Switching back should restore Stats for the directory tab");
    waitForComparison(*directory, [&] {
      OccupiedPool occupied;
      directory->compareDirs();
      check(!stats->isEnabled(), "Stats should be disabled while a new comparison is pending");
      cancelJob(*directory);
    });
    check(!stats->isEnabled(), "A canceled comparison should leave Stats disabled");
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

    auto* newFileDiff = actionNamed(window, "File Diff");
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

  void localCopy(const QDir& left, const QDir& right)
  {
    writeFile(left, "batch/changed.txt", "left");
    writeFile(right, "batch/changed.txt", "right");
    writeFile(left, "batch/left-only.txt", "left only");
    writeFile(right, "batch/right-only.txt", "right only");
    writeFile(left, "created/sub/file.txt", "new folder contents");
    writeFile(left, "unrelated/file.txt", "unrelated");
    QDirDiffWidget widget;
    compare(widget, left, right);
    auto* model = widget.proxyModel();
    int resets = 0;
    QObject::connect(model->sourceModel(), &QAbstractItemModel::modelReset, &widget, [&] { ++resets; });
    auto* view = selectLeftRow(widget, "created");
    const QPersistentModelIndex unrelated(rowNamed(model, "unrelated"));
    view->collapse(unrelated);
    const auto sub = rowNamed(model, "sub", 0, view->currentIndex());
    const auto file = rowNamed(model, "file.txt", 0, sub);
    check(file.isValid(), "Nested one-sided copy source is missing");
    view->selectionModel()->setCurrentIndex(file, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    waitForComparison(widget, [&] { startCopy(widget); });
    check(readFile(right, "created/sub/file.txt") == "new folder contents", "Nested file copy failed");
    check(!rowNamed(model, "created").isValid(), "A resolved one-sided branch should disappear");
    check(resets == 0 && unrelated.isValid() && !view->isExpanded(unrelated),
      "A successful copy should preserve the rest of the view");

    copyLeftRow(widget, "batch");
    const auto batch = rowNamed(model, "batch");
    check(batch.isValid() && !rowNamed(model, "changed.txt", 0, batch).isValid() &&
      !rowNamed(model, "left-only.txt", 0, batch).isValid(), "Successful copies should disappear from the view");
    check(rowNamed(model, "right-only.txt", 2, batch).isValid(), "Copy should preserve destination-only rows");
    check(readFile(right, "batch/changed.txt") == "left", "The changed file was not copied");

    selectLeftRow(widget, "batch");
    waitForComparison(widget, [&] { startCopy(widget, false); });
    check(readFile(left, "batch/right-only.txt") == "right only" && !rowNamed(model, "batch").isValid(),
      "A second copy should use the updated fingerprints and resolve the folder");
    check(resets == 0 && widget.hasComparisonStats(), "Successful folder copies should keep the current comparison");

    waitForComparison(widget, [&] { widget.compareDirs(); });
    check(rowNamed(model, "batch").isValid(), "An explicit comparison should display matching entries again");
  }

  void defaultFileCompare(const QDir& left, const QDir& right)
  {
    writeFile(left, "folder/one-sided.txt", "left only");
    check(QDir().mkpath(right.path()), "Could not create right fixture root");
    QDirDiffWidget widget;
    widget.resize(900, 500);
    widget.show();
    compare(widget, left, right);
    QApplication::processEvents();
    auto* view = selectLeftRow(widget, "folder");
    const auto folder = view->currentIndex();
    const auto file = rowNamed(view->model(), "one-sided.txt", 0, folder);
    check(file.isValid(), "File comparison fixture row is missing");

    int comparisons = 0;
    bool correctPaths = false;
    QObject::connect(&widget, &QDirDiffWidget::compareFileRequested, &widget,
      [&](const QString& a, const QString& b, bool leftMissing, bool rightMissing) {
        ++comparisons;
        correctPaths = a == left.filePath("folder/one-sided.txt") && b == right.filePath("folder/one-sided.txt") &&
          !leftMissing && rightMissing;
      });
    check(QMetaObject::invokeMethod(view, "doubleClicked", folder) && comparisons == 0,
      "Double-clicking a folder should not open File Diff");
    check(QMetaObject::invokeMethod(view, "doubleClicked", file) && comparisons == 1 && correctPaths,
      "Double-clicking a file should request File Diff for the same pair as the menu");

    bool boldDefault = false;
    const auto position = view->visualRect(file).center();
    QTimer::singleShot(0, view, [&] {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!menu) return;
      auto* action = actionNamed(*menu, "Compare files");
      boldDefault = action && menu->defaultAction() == action && action->font().bold();
      menu->close();
      if (action) action->trigger();
    });
    check(QMetaObject::invokeMethod(view, "customContextMenuRequested", position), "Context menu unavailable");
    check(boldDefault && comparisons == 2 && correctPaths, "Compare files should be the bold default menu action");
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

static void fileArrowPalette()
{
  QFileDiffWidget widget;
  auto* toolbar = widget.findChild<QToolBar*>();
  check(toolbar, "File Diff toolbar is missing");
  QPalette palette = widget.palette();
  const QColor disabledText(128, 128, 128);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);

  for (const QColor foreground : { QColor(Qt::black), QColor(Qt::white) }) {
    palette.setColor(QPalette::Active, QPalette::ButtonText, foreground);
    widget.setPalette(palette);
    for (const auto* name : { "Previous Change", "Next Change", "Apply to Left", "Apply to Right" }) {
      const auto* action = actionNamed(*toolbar, name);
      check(action, "A File Diff arrow action is missing");
      for (const auto mode : { QIcon::Normal, QIcon::Disabled }) {
        const QColor expected = mode == QIcon::Normal ? foreground : disabledText;
        const QImage image = action->icon().pixmap(toolbar->iconSize(), 2.0, mode).toImage();
        bool hasForeground = false;
        bool hasTransparency = false;
        for (int y = 0; y < image.height(); ++y) {
          for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            hasForeground |= pixel == expected;
            hasTransparency |= pixel.alpha() == 0;
          }
        }
        check(hasForeground && hasTransparency, "Arrow should use the current palette and retain transparency");
      }
    }
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
    else if (test == "clipboard-actions")
      clipboardActions(left, right);
    else if (test == "comparison-stats")
      comparisonStats(left, right);
    else if (test == "local-copy")
      localCopy(left, right);
    else if (test == "default-file-compare")
      defaultFileCompare(left, right);
    else if (test == "file-tab-width")
      fileTabWidth();
    else if (test == "file-arrow-palette")
      fileArrowPalette();
    else
      throw std::runtime_error("Unknown regression");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
