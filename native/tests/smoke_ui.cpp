// Drives the real window offscreen: import, click, zoom, pan, persistence, PDF export.
// Writes a screenshot to $ALBUM_SMOKE_PNG (default: smoke.png in the temp dir) for eyeballing.
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <cstdio>

#include "exif_jpeg.h"
#include "mainwindow.h"
#include "sheetsview.h"
#include "trayview.h"

using namespace album;
using namespace album::ui;

static int failures = 0, checks = 0;
#define CHECK(cond, msg)                                                         \
  do {                                                                           \
    checks++;                                                                    \
    if (!(cond)) {                                                               \
      std::printf("FAIL %s:%d  %s  (%s)\n", __FILE__, __LINE__, msg, #cond);     \
      failures++;                                                                \
    }                                                                            \
  } while (0)

static void pump(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

static QString makeJpeg(const QString& dir, int i, int w, int h) {
  QImage img(w, h, QImage::Format_RGB32);
  QPainter p(&img);
  p.fillRect(img.rect(), QColor::fromHsv((i * 37) % 360, 190, 225));
  p.setPen(QPen(Qt::white, std::max(w, h) / 40));
  p.drawLine(0, 0, w, h);
  p.drawLine(w, 0, 0, h);
  p.setPen(Qt::black);
  QFont f;
  f.setPixelSize(h / 4);
  p.setFont(f);
  p.drawText(img.rect(), Qt::AlignCenter, QString::number(i + 1));
  p.end();
  const QString path = QString("%1/photo%2.jpg").arg(dir).arg(i + 1, 2, 10, QChar('0'));
  QImageWriter(path, "jpg").write(img);
  return path;
}

// Answers the window's questions without a human, and records which ones were asked.
struct Dialogs {
  QMessageBox::StandardButton answer = QMessageBox::Discard;
  QString savePath;
  QStringList log;
};

static Options opts(const QString& dataDir, Dialogs& d, const QString& project = {}, int recoveryMs = 0) {
  Options o;
  o.dataDir = dataDir;
  o.project = project;
  if (recoveryMs) o.recoveryIntervalMs = recoveryMs;
  o.dialog = [&d](const QString& title, const QString&, QMessageBox::StandardButtons buttons,
                  QMessageBox::StandardButton fallback) {
    d.log << title;
    if (buttons & d.answer) return d.answer;
    return fallback != QMessageBox::NoButton ? fallback : QMessageBox::Ok;
  };
  o.chooseSavePath = [&d](const QString&) { return d.savePath; };
  return o;
}

static void importAndWait(MainWindow& w, const QStringList& files) {
  QEventLoop loop;
  QObject::connect(&w, &MainWindow::importFinished, &loop, &QEventLoop::quit);
  w.importPaths(files);
  loop.exec();
}

static void projectLifecycle(const QString& root) {
  Dialogs d;
  QStringList setA, setB;
  for (int i = 0; i < 3; i++) setA << makeJpeg(root, 20 + i, 1500, 1000);
  for (int i = 0; i < 2; i++) setB << makeJpeg(root, 30 + i, 1000, 1500);
  const QString A = root + "/projA.album", B = root + "/projB.album";
  const QString data = root + "/pdata";
  auto next = [](MainWindow& w) {
    auto* sv = w.sheets();
    QTest::mouseClick(sv->viewport(), Qt::LeftButton, {}, sv->headerButtonPos(0, 1));
  };
  auto prev = [](MainWindow& w) {
    auto* sv = w.sheets();
    QTest::mouseClick(sv->viewport(), Qt::LeftButton, {}, sv->headerButtonPos(0, 0));
  };

  {
    MainWindow w(opts(data, d));
    w.show();
    CHECK(w.projectPath().isEmpty() && !w.isDirty(), "starts as a clean, untitled project");
    CHECK(w.windowTitle().startsWith("Untitled"), "title says Untitled");
    importAndWait(w, setA);
    CHECK(w.isDirty() && w.isWindowModified(), "importing marks the project unsaved");

    d.answer = QMessageBox::Cancel;
    d.log.clear();
    CHECK(!w.close(), "Cancel stops the window from closing");
    CHECK(d.log == QStringList{"Unsaved changes"}, "closing an unsaved project asks first");

    d.savePath = root + "/projA";  // no extension: .album is added
    CHECK(w.saveProjectAs(d.savePath), "save as");
    CHECK(w.projectPath() == A && QFileInfo::exists(A), "the .album extension is added");
    CHECK(!w.isDirty() && !w.isWindowModified() && w.windowTitle().startsWith("projA"),
          "saving clears the marker and names the window after the file");

    const int savedTpl = w.album().spreads[0].tpl;
    next(w);
    CHECK(w.isDirty(), "an edit marks the project unsaved");
    prev(w);
    CHECK(!w.isDirty(), "returning to the saved state clears the marker again");

    next(w);
    d.answer = QMessageBox::Cancel;
    CHECK(!w.newProject() && w.projectPath() == A && w.album().photos.size() == 3,
          "Cancel keeps the current project");
    d.answer = QMessageBox::Discard;
    CHECK(w.newProject(), "new project after Discard");
    CHECK(w.projectPath().isEmpty() && w.album().isEmpty() && !w.isDirty(), "a fresh, clean, untitled project");
    Album onDisk;
    CHECK(Album::loadFile(A, onDisk) && onDisk.spreads[0].tpl == savedTpl, "Discard leaves the file as it was saved");

    // Two parallel projects, switched between freely.
    importAndWait(w, setB);
    d.savePath = B;
    CHECK(w.saveProjectAs(B) && w.album().photos.size() == 2, "second project saved");
    CHECK(w.openProject(A) && w.projectPath() == A && w.album().photos.size() == 3, "open project A");
    CHECK(w.openProject(B) && w.projectPath() == B && w.album().photos.size() == 2, "open project B");

    // Switching with unsaved changes can save them first.
    CHECK(w.openProject(A), "back to A");
    const int tpl0 = w.album().spreads[0].tpl;
    next(w);
    d.answer = QMessageBox::Save;
    CHECK(w.openProject(B), "switch with Save");
    Album savedA;
    CHECK(Album::loadFile(A, savedA) && savedA.spreads[0].tpl != tpl0, "Save wrote the edit to A before switching");
    CHECK(w.projectPath() == B && !w.isDirty(), "now on B, clean");

    // A bad file must not cost the user the open project.
    QFile bad(root + "/bad.album");
    CHECK(bad.open(QIODevice::WriteOnly), "write bad file");
    bad.write("this is not json");
    bad.close();
    d.log.clear();
    CHECK(!w.openProject(root + "/bad.album") && w.projectPath() == B && w.album().photos.size() == 2,
          "a bad file leaves the open project alone");
    CHECK(d.log == QStringList{"Could not open project"}, "and says so");

    QStringList recent;
    for (QMenu* m : w.findChildren<QMenu*>())
      if (m->title().contains("Recent"))
        for (QAction* a : m->actions()) recent << a->text();
    CHECK(recent.contains("projA.album") && recent.contains("projB.album"), "Open Recent lists both projects");
  }

  {
    MainWindow w(opts(data, d));
    CHECK(w.projectPath() == B && w.album().photos.size() == 2 && !w.isDirty(), "relaunch reopens the last project");
  }

  {
    // The last project was deleted: launch untitled without complaining.
    const QString data2 = root + "/pdata2", C = root + "/projC.album";
    {
      MainWindow w(opts(data2, d));
      importAndWait(w, setB);
      d.savePath = C;
      CHECK(w.saveProjectAs(C), "save C");
    }
    QFile::remove(C);
    d.log.clear();
    MainWindow w(opts(data2, d));
    CHECK(w.projectPath().isEmpty() && w.album().isEmpty() && d.log.isEmpty(), "missing last project: untitled, no fuss");
  }

  {
    d.log.clear();
    MainWindow w(opts(root + "/pdata3", d, root + "/nope.album"));
    CHECK(w.projectPath().isEmpty() && d.log == QStringList{"Could not open project"},
          "an explicitly requested missing project is reported");
  }

  {
    // Crash recovery: a recovery copy of a dirty project comes back after the window "crashes".
    const QString data3 = root + "/pdata4", D = root + "/projD.album";
    int tplEdited = 0;
    {
      MainWindow w(opts(data3, d));
      w.show();
      importAndWait(w, setA);
      d.savePath = D;
      CHECK(w.saveProjectAs(D), "save D");
      next(w);
      tplEdited = w.album().spreads[0].tpl;
      QTest::qSleep(30);
      w.writeRecovery();
      CHECK(QFileInfo::exists(w.recoveryFile(D)), "a dirty project gets a recovery copy");
    }  // destroyed without closeEvent, like a crash

    d.log.clear();
    d.answer = QMessageBox::Yes;
    {
      MainWindow w(opts(data3, d));
      CHECK(w.projectPath() == D, "relaunch reopens D");
      CHECK(d.log == QStringList{"Recover unsaved work?"}, "and offers the recovery copy");
      CHECK(w.album().spreads[0].tpl == tplEdited && w.isDirty(), "recovered work returns as unsaved changes");
      CHECK(w.saveProject() && !QFileInfo::exists(w.recoveryFile(D)), "saving removes the recovery copy");
    }

    {
      MainWindow w(opts(data3, d, D));
      w.show();
      next(w);
      QTest::qSleep(30);
      w.writeRecovery();
    }
    d.answer = QMessageBox::No;
    d.log.clear();
    {
      MainWindow w(opts(data3, d));
      CHECK(d.log == QStringList{"Recover unsaved work?"}, "asked again");
      CHECK(w.album().spreads[0].tpl == tplEdited && !w.isDirty(), "declining keeps the saved version");
      CHECK(!QFileInfo::exists(w.recoveryFile(D)), "and discards the recovery copy");
    }
  }

  {
    // Untitled work is protected too, and a clean exit leaves no recovery copy behind.
    const QString data4 = root + "/pdata5";
    {
      MainWindow w(opts(data4, d));
      w.show();
      importAndWait(w, setA);
      w.writeRecovery();
      CHECK(QFileInfo::exists(w.recoveryFile({})), "untitled work gets a recovery copy");
      d.answer = QMessageBox::Discard;
      CHECK(w.close(), "Discard lets the window close");
      CHECK(!QFileInfo::exists(w.recoveryFile({})), "a clean exit removes the recovery copy");
    }
    {
      MainWindow w(opts(data4, d));
      w.show();
      importAndWait(w, setA);
      w.writeRecovery();
    }  // crash
    d.answer = QMessageBox::Yes;
    MainWindow w(opts(data4, d));
    CHECK(w.projectPath().isEmpty() && w.album().photos.size() == 3 && w.isDirty(), "untitled work is recovered");
  }

  {
    // The 5-minute timer (shortened here) writes the copy on its own.
    MainWindow w(opts(root + "/pdata6", d, {}, 50));
    w.show();
    importAndWait(w, setA);
    pump(300);
    CHECK(QFileInfo::exists(w.recoveryFile({})), "the timer writes recovery copies by itself");
  }

  {
    // The single hidden autosave of earlier versions is adopted once.
    const QString data7 = root + "/pdata7";
    QDir().mkpath(data7);
    CHECK(QFile::copy(A, data7 + "/album.json"), "stage legacy autosave");
    {
      MainWindow w(opts(data7, d));
      CHECK(w.projectPath().isEmpty() && w.album().photos.size() == 3 && w.isDirty(),
            "the legacy autosave opens as an unsaved untitled project");
    }
    MainWindow w(opts(data7, d));
    CHECK(w.album().isEmpty(), "and only once");
  }
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir tmp;
  const QString project = tmp.path() + "/first.album";
  Dialogs dlg;

  QStringList files;
  const int dims[][2] = {{3000, 2000}, {2000, 3000}, {3000, 2000}, {2400, 2400},
                         {3000, 2000}, {2000, 3000}, {6000, 1500}, {3000, 2000}};
  for (int i = 0; i < 8; i++) files << makeJpeg(tmp.path(), i, dims[i][0], dims[i][1]);

  {
    MainWindow w(opts(tmp.path() + "/data1", dlg));
    w.resize(1500, 950);
    w.show();
    QEventLoop loop;
    QObject::connect(&w, &MainWindow::importFinished, &loop, &QEventLoop::quit);
    w.importPaths(files);
    loop.exec();

    CHECK(w.album().photos.size() == 8, "all photos imported");
    const std::size_t spreads = w.album().spreads.size();
    CHECK(spreads == 4, "4 + 2 photos, the isolated panorama, then the last photo");
    pump(800);  // let thumbnails and previews arrive

    const QString png = qEnvironmentVariable("ALBUM_SMOKE_PNG", tmp.path() + "/smoke.png");
    CHECK(w.grab().save(png), "screenshot written");
    std::printf("screenshot: %s\n", qPrintable(png));

    {
      // Take photo 8 out of its slot: its tray thumbnail must now carry the gold "unused" tag.
      auto& first = w.album().spreads.front();
      const std::string gone = first.items.back().pid;
      first.items.pop_back();
      w.album().refit(first);
      emit w.sheets()->albumChanged();
      pump(300);
      const QImage shot = w.tray()->grab().toImage();
      const auto idx = std::find_if(w.album().photos.begin(), w.album().photos.end(),
                                    [&](const PhotoRec& p) { return p.id == gone; }) - w.album().photos.begin();
      const double cell = (w.tray()->viewport()->width() - 2.0 * 8 - 6) / 2;
      const int x = int(8 + (idx % 2) * (cell + 6) + cell / 2), y = int(8 + (idx / 2) * (cell + 6) + cell - 4);
      const QColor c = shot.pixelColor(x, y);
      shot.save(qEnvironmentVariable("ALBUM_SMOKE_TRAY_PNG", tmp.path() + "/tray.png"));
      CHECK(c.red() > 150 && c.green() > 100 && c.blue() < 90, "unused tag is painted on the tray thumbnail");
    }

    SheetsView* sv = w.sheets();
    // "+ Spread after" through a real click.
    QTest::mouseClick(sv->viewport(), Qt::LeftButton, {}, sv->headerButtonPos(0, 4));
    CHECK(w.album().spreads.size() == spreads + 1, "+ Spread after inserts a spread");
    CHECK(w.album().spreads[1].items.size() == 1 && w.album().spreads[1].items[0].pid.empty(), "new spread is empty");
    QTest::mouseClick(sv->viewport(), Qt::LeftButton, {}, sv->headerButtonPos(1, 5));
    CHECK(w.album().spreads.size() == spreads, "Delete spread removes it");

    // Ctrl+Z / Ctrl+Shift+Z through real key presses: undo the delete, undo the insert, redo both.
    QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
    CHECK(w.album().spreads.size() == spreads + 1, "Ctrl+Z brings the deleted spread back");
    QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
    CHECK(w.album().spreads.size() == spreads, "Ctrl+Z again removes the inserted spread");
    QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(w.album().spreads.size() == spreads + 1, "Ctrl+Shift+Z redoes the insert");
    QTest::keyClick(&w, Qt::Key_Y, Qt::ControlModifier);
    CHECK(w.album().spreads.size() == spreads, "Ctrl+Y redoes the delete");

    // Next layout button cycles the template.
    const int tpl0 = w.album().spreads[0].tpl;
    QTest::mouseClick(sv->viewport(), Qt::LeftButton, {}, sv->headerButtonPos(0, 1));
    CHECK(w.album().spreads[0].tpl != tpl0, "next layout changes the template");

    // Ctrl+wheel zooms, drag pans, double-click resets.
    const QPoint c = sv->slotCenterPos(0, 0);
    QTest::mouseMove(sv->viewport(), c);
    QWheelEvent wheel(QPointF(c), QPointF(sv->viewport()->mapToGlobal(c)), {}, QPoint(0, 240),
                      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(sv->viewport(), &wheel);
    Item& item = w.album().spreads[0].items[0];
    CHECK(item.zoom > 1.5, "ctrl+wheel zooms in");
    const double x0 = item.pos[0];
    QTest::mousePress(sv->viewport(), Qt::LeftButton, {}, c);
    QTest::mouseMove(sv->viewport(), c + QPoint(-60, 0));
    QTest::mouseRelease(sv->viewport(), Qt::LeftButton, {}, c + QPoint(-60, 0));
    CHECK(item.pos[0] > x0, "dragging left pans the photo to show more of its right side");
    QTest::mouseDClick(sv->viewport(), Qt::LeftButton, {}, c);
    CHECK(item.zoom == 1 && item.pos[0] == 50, "double-click resets framing");

    pump(300);

    // Scroll handling: the last spread is reachable and clickable.
    sv->verticalScrollBar()->setValue(sv->verticalScrollBar()->maximum());
    pump(100);
    const int last = int(w.album().spreads.size()) - 1;
    const std::size_t itemsBefore = w.album().spreads[std::size_t(last)].items.size();
    QTest::mouseClick(sv->viewport(), Qt::LeftButton, {}, sv->headerButtonPos(last, 3));
    CHECK(w.album().spreads[std::size_t(last)].items.size() == itemsBefore + 1, "+ Photo works on the last spread");

    CHECK(w.saveProjectAs(project), "project saved");
  }

  {
    // A second window opening that project restores the album.
    MainWindow w2(opts(tmp.path() + "/data1", dlg, project));
    CHECK(w2.album().photos.size() == 8, "photos restored");
    CHECK(!w2.album().spreads.empty(), "spreads restored");
    w2.show();
    pump(300);
    const QString pdf = tmp.path() + "/out.pdf";
    w2.exportPdfTo(pdf);
    QFile f(pdf);
    CHECK(f.open(QIODevice::ReadOnly) && f.readAll().startsWith("%PDF"), "export wrote a PDF");
    std::printf("pdf bytes: %lld\n", (long long)QFileInfo(pdf).size());

    // The real "Facing pages" checkbox switches the canvas to a single page, and undo undoes it.
    auto* facing = w2.findChild<QCheckBox*>();
    CHECK(facing && facing->isChecked(), "facing pages is on by default");
    if (facing) {
      const double facingW = w2.album().spreadSize().width();
      facing->click();
      CHECK(!w2.album().facing && std::abs(w2.album().spreadSize().width() * 2 - facingW) < 1e-9,
            "unticking makes a spread one page wide");
      pump(300);
      w2.grab().save(qEnvironmentVariable("ALBUM_SMOKE_SINGLE_PNG", tmp.path() + "/single.png"));
      w2.activateWindow();
      CHECK(QTest::qWaitForWindowActive(&w2), "window became active");
      QTest::keyClick(&w2, Qt::Key_Z, Qt::ControlModifier);
      CHECK(w2.album().facing && facing->isChecked(), "Ctrl+Z restores facing pages and the checkbox");
    }
  }

  {
    // An album saved by the old build (unrotated sizes, no version) is repaired on load.
    QImage raw(3000, 2000, QImage::Format_RGB32);
    raw.fill(Qt::darkCyan);
    const QString exif = tmp.path() + "/camera.jpg";
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    raw.save(&buf, "JPG");
    QFile out(exif);
    CHECK(out.open(QIODevice::WriteOnly), "write exif jpeg");
    out.write(withExifOrientation(buf.data(), 6));
    out.close();

    const QString oldData = tmp.path() + "/old.json";
    QFile old(oldData);
    CHECK(old.open(QIODevice::WriteOnly), "write v1 album");
    old.write(QJsonDocument(QJsonObject{
        {"size", 2}, {"bleed", 0.125}, {"gutter", 0.15}, {"perSpread", 4},
        {"photos", QJsonArray{QJsonObject{{"id", "p1"}, {"key", exif}, {"aspect", 1.5}, {"w", 3000}, {"h", 2000}}}},
        {"spreads", QJsonArray{QJsonObject{{"tpl", 0}, {"items", QJsonArray{QJsonObject{
            {"pid", "p1"}, {"pos", QJsonArray{50, 50}}, {"zoom", 1}}}}}}}}).toJson());
    old.close();

    MainWindow fixed(opts(tmp.path() + "/data2", dlg, oldData));
    const PhotoRec* p = fixed.album().photo("p1");
    CHECK(p && p->w == 2000 && p->h == 3000, "v1 sizes are re-read as rotated");
    CHECK(p && std::abs(p->aspect - 2.0 / 3) < 1e-9, "aspect repaired");
    CHECK(fixed.album().templateOf(fixed.album().spreads[0])[0].w < 0.5, "layout re-picked for a portrait");
  }

  projectLifecycle(tmp.path());

  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("ui smoke: all %d checks passed\n", checks);
  return 0;
}
