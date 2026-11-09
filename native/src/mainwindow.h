#pragma once

#include <QElapsedTimer>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <functional>
#include <memory>

#include "album.h"
#include "history.h"
#include "imagecache.h"

class QAction;
class QMenu;

class QComboBox;
class QCheckBox;
class QLabel;
class QDoubleSpinBox;

namespace album::ui {

class SheetsView;
class TrayView;
class OutlineView;

/// Replaceable so tests can answer the window's questions without a human.
using DialogFn = std::function<QMessageBox::StandardButton(
    const QString& title, const QString& text, QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton fallback)>;

struct Options {
  QString project;   // project file to open instead of the last one
  QString dataDir;   // settings, recent list and recovery copies (default: app data folder)
  DialogFn dialog;   // default: real message boxes
  std::function<QString(const QString& suggestedPath)> chooseSavePath;  // default: file dialog
  int recoveryIntervalMs = 5 * 60 * 1000;
};

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(Options options = {});
  ~MainWindow() override;

  /// Files and folders (folders are scanned recursively). Probing runs on worker threads.
  void importPaths(const QStringList& paths);
  void exportPdfTo(const QString& path);

  Album& album() { return album_; }
  SheetsView* sheets() { return sheets_; }
  TrayView* tray() { return tray_; }

  /// Projects. Each returns false if cancelled or failed (and says why in the status bar).
  /// Anything with unsaved changes asks Save / Discard / Cancel first.
  bool newProject();
  bool openProject(const QString& path);
  bool saveProject();                           // to the current file; false if untitled
  bool saveProjectAs(const QString& path);
  const QString& projectPath() const { return projectPath_; }
  bool isDirty() const { return dirty_; }
  void writeRecovery();                         // what the 5-minute timer does
  QString recoveryFile(const QString& projectPath) const;

 signals:
  void importFinished();

 protected:
  void closeEvent(QCloseEvent*) override;
  void dragEnterEvent(QDragEnterEvent*) override;
  void dropEvent(QDropEvent*) override;

 private:
  void buildUi();
  void buildMenus();
  void syncControls();
  void updateSizeLabels();
  void refresh();   // model changed: repaint every view and update the unsaved marker
  /// An edit just happened: record it for undo, then refresh. Edits sharing a non-empty
  /// `kind` in quick succession merge into one undo step.
  void commit(const char* kind = "");
  void undo();
  void redo();
  void updateUndoActions();

  // projects
  void startup(const QString& explicitPath);
  void adopt(Album a, const QString& path, bool clean);
  bool maybeSave();
  bool saveInteractive();                       // Save, asking for a file name if untitled
  QString pickSavePath();
  bool offerRecovery(const QString& projectPath, Album& into);
  void actOpen();
  void actSaveAs();
  void handleDropped(const QStringList& paths);
  void markDirty();
  void updateTitle();
  void rememberProject(const QString& path);
  void rebuildRecentMenu();
  void removeRecovery(const QString& projectPath);
  void repairOldSizes();
  void reportMissing();
  QMessageBox::StandardButton ask(const QString& title, const QString& text,
                                  QMessageBox::StandardButtons buttons,
                                  QMessageBox::StandardButton fallback = QMessageBox::NoButton);

  void finishImport(const QList<ProbeResult>& results);
  void exportPdf();
  void clearAll();
  void removePhoto(const QString& id);

  Album album_;
  ImageCache cache_;
  Options options_;
  QString dataDir_;
  std::unique_ptr<QSettings> settings_;
  QString projectPath_;      // empty = untitled
  QString note_;             // one-off message shown after start-up
  Snapshot saved_;           // the state on disk; differs from album_ = unsaved changes
  bool dirty_ = false;
  QTimer recoveryTimer_;
  bool importing_ = false;
  History history_;
  QElapsedTimer clock_;
  QAction* undoAct_ = nullptr;
  QAction* redoAct_ = nullptr;
  QMenu* recentMenu_ = nullptr;

  SheetsView* sheets_ = nullptr;
  TrayView* tray_ = nullptr;
  OutlineView* outline_ = nullptr;
  QComboBox* size_ = nullptr;
  QComboBox* perSpread_ = nullptr;
  QDoubleSpinBox* bleed_ = nullptr;
  QDoubleSpinBox* gutter_ = nullptr;
  QCheckBox* facing_ = nullptr;
  QLabel* perSpreadLabel_ = nullptr;
};

}  // namespace album::ui
