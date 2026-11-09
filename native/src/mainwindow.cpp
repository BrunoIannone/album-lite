#include "mainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QCollator>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QtConcurrent>
#include <algorithm>

#include "exporter.h"
#include "outlineview.h"
#include "sheetsview.h"
#include "trayview.h"

namespace album::ui {

namespace {

QStringList imageFilters() {
  QStringList out;
  for (const QByteArray& f : QImageReader::supportedImageFormats()) out << "*." + QString::fromLatin1(f);
  return out;
}

// Natural order ("img2" before "img10"), so folder imports follow shooting order.
QStringList expandPaths(const QStringList& paths) {
  QStringList files;
  QCollator collator;
  collator.setNumericMode(true);
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  for (const QString& path : paths) {
    const QFileInfo info(path);
    if (info.isDir()) {
      QStringList found;
      QDirIterator it(info.absoluteFilePath(), imageFilters(), QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext()) found << it.next();
      std::sort(found.begin(), found.end(), collator);
      files << found;
    } else if (info.isFile()) {
      files << info.absoluteFilePath();
    }
  }
  return files;
}

}  // namespace

MainWindow::MainWindow(Options options) : options_(std::move(options)) {
  dataDir_ = options_.dataDir.isEmpty()
                 ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 : options_.dataDir;
  QDir().mkpath(dataDir_ + "/recovery");
  settings_ = std::make_unique<QSettings>(dataDir_ + "/settings.ini", QSettings::IniFormat);
  resize(1500, 950);
  setAcceptDrops(true);
  clock_.start();

  startup(options_.project);  // picks the album to show; the views are built right after
  buildUi();
  buildMenus();
  syncControls();
  refresh();
  updateTitle();
  reportMissing();
  if (!note_.isEmpty()) statusBar()->showMessage(note_);

  // A crash costs at most one interval of work: dirty projects get a recovery copy.
  recoveryTimer_.setInterval(options_.recoveryIntervalMs);
  connect(&recoveryTimer_, &QTimer::timeout, this, &MainWindow::writeRecovery);
  recoveryTimer_.start();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenus() {
  auto* file = menuBar()->addMenu("&File");
  file->addAction("&New project", QKeySequence::New, this, [this] { newProject(); });
  file->addAction("&Open project…", QKeySequence::Open, this, [this] { actOpen(); });
  recentMenu_ = file->addMenu("Open &Recent");
  file->addSeparator();
  file->addAction("&Save", QKeySequence::Save, this, [this] { saveInteractive(); });
  file->addAction("Save &As…", QKeySequence::SaveAs, this, [this] { actSaveAs(); });
  file->addSeparator();
  file->addAction("&Quit", QKeySequence::Quit, this, [this] { close(); });
  rebuildRecentMenu();
}

void MainWindow::buildUi() {
  auto* tb = addToolBar("Main");
  tb->setMovable(false);
  tb->setContextMenuPolicy(Qt::PreventContextMenu);

  auto labelled = [&](const QString& text, QWidget* w, const QString& tip = {}) {
    auto* l = new QLabel(" " + text + " ");
    l->setToolTip(tip);
    w->setToolTip(tip);
    tb->addWidget(l);
    tb->addWidget(w);
    tb->addSeparator();
    return l;
  };

  auto* title = new QLabel("<b>Album Lite</b>");
  tb->addWidget(title);
  tb->addSeparator();

  size_ = new QComboBox;
  for (std::size_t i = 0; i < pageSizes().size(); i++) size_->addItem({});
  updateSizeLabels();
  labelled("Size", size_);

  facing_ = new QCheckBox("Facing pages");
  facing_->setToolTip("On: each spread is two facing pages with a spine.\n"
                      "Off: each spread is a single page of the chosen size.");
  tb->addWidget(facing_);
  tb->addSeparator();

  perSpread_ = new QComboBox;
  for (int i = 1; i <= kMaxPerSpread; i++) perSpread_->addItem(QString::number(i), i);
  perSpreadLabel_ = labelled("Photos /", perSpread_);
  updateSizeLabels();

  bleed_ = new QDoubleSpinBox;
  bleed_->setRange(0, 0.5);
  bleed_->setSingleStep(0.0625);
  bleed_->setDecimals(4);
  labelled("Bleed in", bleed_, "Extra image area beyond the trim edge, required by most print labs");

  gutter_ = new QDoubleSpinBox;
  gutter_->setRange(0, 1);
  gutter_->setSingleStep(0.05);
  gutter_->setDecimals(2);
  labelled("Gutter in", gutter_, "Space between photos");

  auto* spacer = new QWidget;
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  tb->addWidget(spacer);

  // Ctrl+Z / Ctrl+Shift+Z (and Ctrl+Y). Window-level shortcuts, so they work wherever focus
  // is, except inside a text box, which keeps its own text undo.
  undoAct_ = tb->addAction("↶ Undo", this, &MainWindow::undo);
  undoAct_->setShortcut(QKeySequence::Undo);
  undoAct_->setToolTip("Undo (Ctrl+Z)");
  redoAct_ = tb->addAction("↷ Redo", this, &MainWindow::redo);
  redoAct_->setShortcuts({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")});
  redoAct_->setToolTip("Redo (Ctrl+Shift+Z)");
  updateUndoActions();

  tb->addAction("Import photos…", this, [this] {
    const QString filter = "Images (" + imageFilters().join(' ') + ")";
    const QStringList files = QFileDialog::getOpenFileNames(this, "Import photos", QDir::homePath(), filter);
    if (!files.isEmpty()) importPaths(files);
  });
  tb->addAction("Auto-layout", this, [this] {
    album_.rebuild();
    commit();
  });
  tb->addAction("Export PDF", this, [this] { exportPdf(); });
  tb->addAction("Clear", this, [this] { clearAll(); });


  sheets_ = new SheetsView(&album_, &cache_);
  tray_ = new TrayView(&album_, &cache_);
  outline_ = new OutlineView(&album_, &cache_);
  auto* split = new QSplitter;
  split->addWidget(tray_);
  split->addWidget(sheets_);
  split->addWidget(outline_);
  split->setStretchFactor(1, 1);
  split->setSizes({190, 1160, 150});
  split->setChildrenCollapsible(false);
  setCentralWidget(split);

  connect(size_, &QComboBox::currentIndexChanged, this, [this](int i) {
    album_.size = i;
    album_.refitAll();
    commit();
  });
  connect(facing_, &QCheckBox::toggled, this, [this](bool on) {
    album_.facing = on;
    album_.refitAll();  // templates are picked by slot shape, and the canvas shape just changed
    updateSizeLabels();
    commit();
  });
  connect(perSpread_, &QComboBox::currentIndexChanged, this, [this](int) {
    album_.perSpread = perSpread_->currentData().toInt();
    album_.rebuild();
    commit();
  });
  connect(bleed_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    album_.bleed = v;
    commit("bleed");
  });
  connect(gutter_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    album_.gutter = v;
    commit("gutter");
  });

  connect(sheets_, &SheetsView::albumChanged, this, [this] { commit(); });
  // Pan, zoom and reset only move pixels: repaint is already done, so just record the step.
  connect(sheets_, &SheetsView::framingChanged, this, [this] {
    history_.record(album_.snapshot(), "framing", clock_.elapsed());
    updateUndoActions();
    markDirty();
  });
  connect(sheets_, &SheetsView::filesDropped, this, &MainWindow::handleDropped);
  connect(tray_, &TrayView::removeRequested, this, &MainWindow::removePhoto);
  connect(outline_, &OutlineView::spreadClicked, sheets_, &SheetsView::scrollToSpread);
  connect(outline_, &OutlineView::reordered, this, [this] { commit(); });
}

void MainWindow::syncControls() {
  const std::initializer_list<QObject*> all{size_, perSpread_, bleed_, gutter_, facing_};
  for (QObject* o : all) o->blockSignals(true);
  size_->setCurrentIndex(album_.size);
  perSpread_->setCurrentIndex(album_.perSpread - 1);
  bleed_->setValue(album_.bleed);
  gutter_->setValue(album_.gutter);
  facing_->setChecked(album_.facing);
  for (QObject* o : all) o->blockSignals(false);
  updateSizeLabels();
}

// Each entry names one page; the second size is what the canvas (and each PDF page) covers.
void MainWindow::updateSizeLabels() {
  if (perSpreadLabel_) perSpreadLabel_->setText(album_.facing ? " Photos / spread " : " Photos / page ");
  const double mult = album_.facing ? 2 : 1;
  for (std::size_t i = 0; i < pageSizes().size(); i++) {
    const PageSize& s = pageSizes()[i];
    size_->setItemText(int(i), album_.facing
        ? QString("%1 page  ·  %2 x %3 in spread").arg(s.name).arg(s.w * mult).arg(s.h)
        : QString("%1  ·  %2 x %3 mm").arg(s.name).arg(std::lround(s.w * 25.4)).arg(std::lround(s.h * 25.4)));
  }
}

void MainWindow::refresh() {
  sheets_->reload();
  tray_->reload();
  outline_->reload();
  markDirty();
}

void MainWindow::commit(const char* kind) {
  history_.record(album_.snapshot(), kind, clock_.elapsed());
  refresh();
  updateUndoActions();
}

void MainWindow::undo() {
  const Snapshot* s = history_.undo();
  if (!s) return;
  album_.restore(*s);
  syncControls();
  refresh();
  updateUndoActions();
  statusBar()->showMessage("Undo");
}

void MainWindow::redo() {
  const Snapshot* s = history_.redo();
  if (!s) return;
  album_.restore(*s);
  syncControls();
  refresh();
  updateUndoActions();
  statusBar()->showMessage("Redo");
}

void MainWindow::updateUndoActions() {
  if (undoAct_) undoAct_->setEnabled(history_.canUndo());
  if (redoAct_) redoAct_->setEnabled(history_.canRedo());
}

/* --- projects ------------------------------------------------------------ */

QMessageBox::StandardButton MainWindow::ask(const QString& title, const QString& text,
                                            QMessageBox::StandardButtons buttons,
                                            QMessageBox::StandardButton fallback) {
  if (options_.dialog) return options_.dialog(title, text, buttons, fallback);
  QMessageBox box(buttons == QMessageBox::Ok ? QMessageBox::Warning : QMessageBox::Question, title, text,
                  buttons, this);
  if (fallback != QMessageBox::NoButton) box.setDefaultButton(fallback);
  return QMessageBox::StandardButton(box.exec());
}

QString MainWindow::recoveryFile(const QString& projectPath) const {
  const QString name = projectPath.isEmpty()
      ? QString("untitled")
      : QString::fromLatin1(QCryptographicHash::hash(QFileInfo(projectPath).absoluteFilePath().toUtf8(),
                                                     QCryptographicHash::Sha1).toHex());
  return dataDir_ + "/recovery/" + name + ".album";
}

void MainWindow::removeRecovery(const QString& projectPath) { QFile::remove(recoveryFile(projectPath)); }

void MainWindow::writeRecovery() {
  if (dirty_) album_.saveFile(recoveryFile(projectPath_));  // best effort: failing must not interrupt work
}

// A recovery copy newer than its project (or any for an untitled album) means the last
// session ended without saving. Declined or unreadable copies are deleted.
bool MainWindow::offerRecovery(const QString& projectPath, Album& into) {
  const QString rec = recoveryFile(projectPath);
  if (!QFileInfo::exists(rec)) return false;
  if (!projectPath.isEmpty() && QFileInfo(rec).lastModified() <= QFileInfo(projectPath).lastModified()) {
    QFile::remove(rec);
    return false;
  }
  const QString what = projectPath.isEmpty() ? QString("an untitled album")
                                             : QString("“%1”").arg(QFileInfo(projectPath).fileName());
  Album recovered;
  if (ask("Recover unsaved work?",
          QString("Unsaved changes to %1 were found from a previous session (%2).\n\nRecover them?")
              .arg(what, QFileInfo(rec).lastModified().toString("d MMM, HH:mm")),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes &&
      Album::loadFile(rec, recovered)) {
    into = std::move(recovered);
    return true;
  }
  QFile::remove(rec);
  return false;
}

void MainWindow::startup(const QString& explicitPath) {
  QString path = explicitPath.isEmpty() ? settings_->value("lastProject").toString() : explicitPath;
  if (!path.isEmpty()) path = QFileInfo(path).absoluteFilePath();

  Album a;
  QString err;
  if (!path.isEmpty() && !Album::loadFile(path, a, &err)) {
    // A missing or unreadable file must not stop the app from launching.
    if (!explicitPath.isEmpty())
      ask("Could not open project", QString("Could not open %1:\n%2").arg(path, err), QMessageBox::Ok);
    path.clear();
    a = Album();
    settings_->remove("lastProject");
  }

  bool clean = !offerRecovery(path, a);

  // Before projects existed the app kept a single hidden autosave; adopt it once.
  if (path.isEmpty() && clean && a.isEmpty() && !settings_->value("legacyImported").toBool()) {
    Album legacy;
    if (Album::loadFile(dataDir_ + "/album.json", legacy) && !legacy.isEmpty()) {
      a = std::move(legacy);
      clean = false;
      note_ = "Your previous album was loaded as an untitled project — use File ▸ Save As to keep it.";
    }
    settings_->setValue("legacyImported", true);
  }
  adopt(std::move(a), path, clean);
}

// Make `a` the current project. `clean` = identical to what is on disk; otherwise it counts
// as unsaved. Before the views exist (constructor) only the model is set up.
void MainWindow::adopt(Album a, const QString& path, bool clean) {
  const bool needsRepair = a.loadedVersion < kFormatVersion;
  album_ = std::move(a);
  projectPath_ = path;
  saved_ = clean ? album_.snapshot() : Album().snapshot();
  if (needsRepair) repairOldSizes();  // after saved_, so the repair itself counts as a change
  history_.reset(album_.snapshot());
  if (!sheets_) return;
  cache_.clear();
  syncControls();
  refresh();
  updateUndoActions();
  updateTitle();
  reportMissing();
}

void MainWindow::reportMissing() {
  const int missing = int(std::count_if(album_.photos.begin(), album_.photos.end(),
                                        [](const PhotoRec& p) { return !p.exists; }));
  if (missing) statusBar()->showMessage(QString("%1 photo%2 missing — re-import to restore this album.")
                                            .arg(missing).arg(missing > 1 ? "s" : ""));
}

void MainWindow::markDirty() {
  const bool d = !(album_.snapshot() == saved_);
  if (d == dirty_) return;
  dirty_ = d;
  updateTitle();
}

void MainWindow::updateTitle() {
  const QString name = projectPath_.isEmpty() ? QString("Untitled") : QFileInfo(projectPath_).completeBaseName();
  setWindowTitle(name + "[*] — Album Lite");
  setWindowModified(dirty_);
}

void MainWindow::rememberProject(const QString& path) {
  settings_->setValue("lastProject", path);
  settings_->setValue("lastDir", QFileInfo(path).absolutePath());
  QStringList recent = settings_->value("recent").toStringList();
  recent.removeAll(path);
  recent.prepend(path);
  while (recent.size() > 10) recent.removeLast();
  settings_->setValue("recent", recent);
  settings_->sync();
  rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
  if (!recentMenu_) return;
  recentMenu_->clear();
  int shown = 0;
  for (const QString& path : settings_->value("recent").toStringList()) {
    if (!QFileInfo::exists(path)) continue;
    recentMenu_->addAction(QFileInfo(path).fileName(), this, [this, path] { openProject(path); })->setToolTip(path);
    shown++;
  }
  if (!shown) recentMenu_->addAction("(none)")->setEnabled(false);
}

/// True if it is fine to replace or close the current project.
bool MainWindow::maybeSave() {
  if (!dirty_) return true;
  const QString name = projectPath_.isEmpty() ? QString("Untitled") : QFileInfo(projectPath_).fileName();
  switch (ask("Unsaved changes", QString("Save changes to “%1” before continuing?").arg(name),
              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save)) {
    case QMessageBox::Save: return saveInteractive();
    case QMessageBox::Discard: return true;
    default: return false;
  }
}

QString MainWindow::pickSavePath() {
  const QString dir = settings_->value("lastDir", QDir::homePath()).toString();
  const QString name = projectPath_.isEmpty() ? QString("Untitled.album") : QFileInfo(projectPath_).fileName();
  const QString suggested = dir + "/" + name;
  if (options_.chooseSavePath) return options_.chooseSavePath(suggested);
  return QFileDialog::getSaveFileName(this, "Save project", suggested, "Album project (*.album)");
}

bool MainWindow::saveInteractive() {
  if (!projectPath_.isEmpty()) return saveProject();
  const QString path = pickSavePath();
  return !path.isEmpty() && saveProjectAs(path);
}

void MainWindow::actSaveAs() {
  const QString path = pickSavePath();
  if (!path.isEmpty()) saveProjectAs(path);
}

void MainWindow::actOpen() {
  const QString dir = settings_->value("lastDir", QDir::homePath()).toString();
  const QString path = QFileDialog::getOpenFileName(this, "Open project", dir,
                                                    "Album project (*.album);;All files (*)");
  if (!path.isEmpty()) openProject(path);
}

bool MainWindow::saveProject() { return !projectPath_.isEmpty() && saveProjectAs(projectPath_); }

bool MainWindow::saveProjectAs(const QString& path) {
  QString target = QFileInfo(path).absoluteFilePath();
  if (QFileInfo(target).suffix().compare("album", Qt::CaseInsensitive) != 0) target += ".album";
  QString err;
  if (!album_.saveFile(target, &err)) {
    ask("Could not save", QString("Could not save %1:\n%2").arg(target, err), QMessageBox::Ok);
    return false;
  }
  removeRecovery(projectPath_);  // the previous name (or untitled) no longer has unsaved work
  projectPath_ = target;
  removeRecovery(target);
  saved_ = album_.snapshot();
  rememberProject(target);
  markDirty();
  updateTitle();
  statusBar()->showMessage("Saved " + target);
  return true;
}

bool MainWindow::newProject() {
  if (importing_) {
    statusBar()->showMessage("Still importing — try again in a moment.");
    return false;
  }
  if (!maybeSave()) return false;
  removeRecovery(projectPath_);
  settings_->remove("lastProject");  // next launch starts empty, not on the project left behind
  adopt(Album(), {}, true);
  statusBar()->showMessage("New project");
  return true;
}

bool MainWindow::openProject(const QString& path) {
  if (importing_) {
    statusBar()->showMessage("Still importing — try again in a moment.");
    return false;
  }
  const QString abs = QFileInfo(path).absoluteFilePath();
  // Read first, so a bad file never costs the user the project they have open.
  Album a;
  QString err;
  if (!Album::loadFile(abs, a, &err)) {
    ask("Could not open project", QString("Could not open %1:\n%2").arg(abs, err), QMessageBox::Ok);
    return false;
  }
  if (!maybeSave()) return false;
  removeRecovery(projectPath_);
  const bool clean = !offerRecovery(abs, a);
  adopt(std::move(a), abs, clean);
  rememberProject(abs);
  statusBar()->showMessage(QString("Opened %1 · %2 photos, %3 spreads")
                               .arg(abs).arg(album_.photos.size()).arg(album_.spreads.size()));
  reportMissing();
  return true;
}

void MainWindow::handleDropped(const QStringList& paths) {
  if (paths.size() == 1 && QFileInfo(paths[0]).suffix().compare("album", Qt::CaseInsensitive) == 0)
    openProject(paths[0]);
  else
    importPaths(paths);
}

// Albums saved before format 2 recorded unrotated sizes for EXIF-rotated photos, which made
// them render stretched. Re-read the (header-only, cheap) sizes, and re-pick the layout of
// any spread whose photos were affected, since it was chosen from the wrong shapes.
void MainWindow::repairOldSizes() {
  std::unordered_set<std::string> changed;
  for (PhotoRec& p : album_.photos) {
    if (!p.exists) continue;
    const ProbeResult r = probeImage(p.path);
    if (!r.ok || (r.w == p.w && r.h == p.h)) continue;
    p.w = r.w;
    p.h = r.h;
    p.aspect = double(r.w) / r.h;
    changed.insert(p.id);
  }
  for (Spread& sp : album_.spreads)
    for (const Item& it : sp.items)
      if (changed.count(it.pid)) {
        album_.refit(sp);
        break;
      }
  album_.loadedVersion = kFormatVersion;
}

void MainWindow::closeEvent(QCloseEvent* e) {
  if (!maybeSave()) {
    e->ignore();
    return;
  }
  removeRecovery(projectPath_);  // a clean exit leaves no recovery copy behind
  settings_->sync();
  QMainWindow::closeEvent(e);
}

/* --- import -------------------------------------------------------------- */

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
  QStringList paths;
  for (const QUrl& u : e->mimeData()->urls())
    if (u.isLocalFile()) paths << u.toLocalFile();
  if (!paths.isEmpty()) handleDropped(paths);
}

void MainWindow::importPaths(const QStringList& paths) {
  if (importing_) {
    statusBar()->showMessage("Still importing — try again in a moment.");
    return;
  }
  const QStringList files = expandPaths(paths);
  if (files.isEmpty()) return;
  importing_ = true;

  auto* watcher = new QFutureWatcher<ProbeResult>(this);
  connect(watcher, &QFutureWatcherBase::progressValueChanged, this, [this, total = files.size()](int v) {
    statusBar()->showMessage(QString("Importing %1/%2…").arg(v).arg(total));
  });
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
    finishImport(watcher->future().results());
    watcher->deleteLater();
  });
  watcher->setFuture(QtConcurrent::mapped(files, [](const QString& p) { return probeImage(p); }));
}

// A file already referenced by the restored album fills in its blank; anything else is new.
// Results arrive in selection order, so chronology survives the parallel probing.
void MainWindow::finishImport(const QList<ProbeResult>& results) {
  std::vector<Photo> fresh;
  int skipped = 0;
  for (const ProbeResult& r : results) {
    if (!r.ok) {
      skipped++;
      continue;
    }
    PhotoRec* held = album_.findByPath(r.path);
    if (held && held->exists) continue;
    if (held) {
      held->w = r.w;
      held->h = r.h;
      held->aspect = double(r.w) / r.h;
      held->exists = true;
      cache_.forget(r.path);
      continue;
    }
    PhotoRec p;
    p.id = Album::newId();
    p.path = r.path;
    p.w = r.w;
    p.h = r.h;
    p.aspect = double(r.w) / r.h;
    album_.addPhoto(p);
    fresh.push_back({p.id, p.aspect});
  }
  // Only new photos get new spreads; existing spreads keep whatever you arranged.
  const QSizeF sz = album_.spreadSize();
  for (auto& s : flowSpreads(fresh, album_.perSpread, sz.width(), sz.height()))
    album_.spreads.push_back(std::move(s));

  importing_ = false;
  commit();
  QString msg = QString("%1 photos, %2 spreads").arg(album_.photos.size()).arg(album_.spreads.size());
  if (skipped) msg += QString(" · %1 file%2 skipped (not readable images)").arg(skipped).arg(skipped > 1 ? "s" : "");
  statusBar()->showMessage(msg);
  emit importFinished();
}

void MainWindow::removePhoto(const QString& id) {
  const PhotoRec* p = album_.photo(id.toStdString());
  if (!p) return;
  const QString path = p->path, name = Album::label(*p);
  album_.removePhoto(id.toStdString());
  cache_.forget(path);
  commit();
  statusBar()->showMessage(name + " removed from album.");
}

void MainWindow::clearAll() {
  if (QMessageBox::question(this, "Clear album", "Remove all photos and spreads?") != QMessageBox::Yes) return;
  album_.clear();
  cache_.clear();
  commit();
  statusBar()->clearMessage();
}

/* --- export -------------------------------------------------------------- */

void MainWindow::exportPdf() {
  if (album_.spreads.empty()) {
    statusBar()->showMessage("Nothing to export.");
    return;
  }
  const auto soft = album_.lowRes();
  if (!soft.empty()) {
    QString msg = QString("%1 photo%2 will print below %3 DPI and may look soft:\n\n")
                      .arg(soft.size()).arg(soft.size() > 1 ? "s" : "").arg(int(kMinDpi));
    for (std::size_t i = 0; i < soft.size() && i < 8; i++) msg += QString("  %1 — %2 DPI\n").arg(soft[i].name).arg(soft[i].dpi);
    if (soft.size() > 8) msg += QString("  …and %1 more\n").arg(soft.size() - 8);
    msg += "\nZoom those out, or export anyway?";
    if (QMessageBox::question(this, "Low resolution", msg, QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel) != QMessageBox::Yes) {
      statusBar()->showMessage(QString("Export cancelled — %1 photo(s) below %2 DPI.").arg(soft.size()).arg(int(kMinDpi)));
      return;
    }
  }
  const PageSize& pg = album_.page();
  const QString def = QDir::homePath() + QString("/album-%1x%2.pdf").arg(pg.w).arg(pg.h);
  const QString path = QFileDialog::getSaveFileName(this, "Export PDF", def, "PDF (*.pdf)");
  if (path.isEmpty()) {
    statusBar()->showMessage("Export cancelled.");
    return;
  }
  exportPdfTo(path.endsWith(".pdf", Qt::CaseInsensitive) ? path : path + ".pdf");
}

void MainWindow::exportPdfTo(const QString& path) {
  QProgressDialog dlg("Exporting spreads…", "Cancel", 0, int(album_.spreads.size()), this);
  dlg.setWindowModality(Qt::WindowModal);
  dlg.setMinimumDuration(0);
  const ExportResult r = album::exportPdf(album_, path, [&](int done, int) {
    dlg.setValue(done);
    QCoreApplication::processEvents();
    return !dlg.wasCanceled();
  });
  dlg.close();
  if (!r.ok) {
    statusBar()->showMessage(r.error);
    return;
  }
  const QSizeF sheet = album_.sheetSize();
  QString msg = QString("Saved %1 · %2 spreads at %3 x %4 in (incl. %5 in bleed)")
                    .arg(path).arg(album_.spreads.size()).arg(sheet.width()).arg(sheet.height()).arg(album_.bleed);
  if (r.skipped) msg += QString(" · %1 photo(s) could not be read").arg(r.skipped);
  statusBar()->showMessage(msg);
}

}  // namespace album::ui
