// Model, image I/O and PDF export against real files in a temp dir (no window needed).
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <cstdio>

#include "album.h"
#include "exif_jpeg.h"
#include "exporter.h"
#include "history.h"
#include "imagecache.h"

using namespace album;

static int failures = 0;
#define CHECK(cond, msg)                                                         \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("FAIL %s:%d  %s  (%s)\n", __FILE__, __LINE__, msg, #cond);     \
      failures++;                                                                \
    }                                                                            \
  } while (0)

static QString makeJpeg(const QString& dir, const QString& name, int w, int h, int hue,
                        QImageIOHandler::Transformation t = QImageIOHandler::TransformationNone) {
  QImage img(w, h, QImage::Format_RGB32);
  QPainter p(&img);
  p.fillRect(img.rect(), QColor::fromHsv(hue, 200, 220));
  p.setPen(Qt::white);
  p.drawLine(0, 0, w, h);
  p.end();
  const QString path = dir + "/" + name;
  QImageWriter wr(path, "jpg");
  wr.setTransformation(t);
  wr.setQuality(90);
  CHECK(wr.write(img), "write test jpeg");
  return path;
}

// Qt's JPEG writer bakes rotation into the pixels and never writes an EXIF tag, so insert a
// real APP1/Orientation segment by hand: this is what camera files look like.
static QString makeExifJpeg(const QString& dir, const QString& name, int w, int h, int orientation) {
  const QString path = makeJpeg(dir, name, w, h, 120);
  QFile f(path);
  CHECK(f.open(QIODevice::ReadOnly), "read jpeg back");
  const QByteArray data = withExifOrientation(f.readAll(), orientation);
  f.close();
  CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "rewrite jpeg");
  f.write(data);
  return path;
}

static void exifRotationIsConsistent(const QString& dir) {
  struct Case { int orientation; bool swaps; };
  for (const Case c : {Case{1, false}, {3, false}, {6, true}, {8, true}, {5, true}, {7, true}}) {
    const QString path = makeExifJpeg(dir, QString("exif%1.jpg").arg(c.orientation), 3000, 2000, c.orientation);
    const QSize want = c.swaps ? QSize(2000, 3000) : QSize(3000, 2000);
    const auto probe = probeImage(path);
    CHECK(probe.ok && QSize(probe.w, probe.h) == want, "probe reports the displayed size");
    CHECK(loadFullImage(path).size() == want, "full decode matches the probe");

    // Every cache tier must keep the displayed aspect, or photos look stretched.
    ImageCache cache;
    int arrivals = 0;
    QObject::connect(&cache, &ImageCache::loaded, [&] { arrivals++; });
    for (int side : {200, 900, 2000}) {
      arrivals = 0;
      QPixmap pm = cache.get(path, side);
      for (int i = 0; i < 300 && arrivals == 0 && pm.isNull(); i++) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(5);
        pm = cache.get(path, side);
      }
      CHECK(!pm.isNull(), "tier decoded");
      const double err = std::abs(double(pm.width()) / pm.height() / (double(want.width()) / want.height()) - 1);
      CHECK(err < 0.01, "tier keeps the displayed aspect ratio");
    }
  }
}

static void projectFiles(const QString& dir) {
  Album a;
  a.size = 3;
  a.facing = false;
  PhotoRec p;
  p.id = Album::newId();
  p.path = makeJpeg(dir, "pf.jpg", 300, 200, 10);
  p.w = 300;
  p.h = 200;
  p.aspect = 1.5;
  a.addPhoto(p);
  a.rebuild();

  const QString path = dir + "/proj.album";
  CHECK(a.saveFile(path), "save project file");
  CHECK(!QFileInfo::exists(path + ".tmp") && QDir(dir).entryList({"*.album.*"}).isEmpty(), "no temp files left behind");
  Album b;
  CHECK(Album::loadFile(path, b) && b.snapshot() == a.snapshot(), "a project file round-trips exactly");

  // A failed save must not destroy the previous file.
  const QByteArray before = [&] { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); }();
  CHECK(!a.saveFile(dir + "/missing-folder/x.album"), "saving into a missing folder fails");
  QString err;
  CHECK(!Album::loadFile(dir + "/nope.album", b, &err) && !err.isEmpty(), "loading a missing file reports why");
  QFile junk(dir + "/junk.album");
  CHECK(junk.open(QIODevice::WriteOnly), "write junk");
  junk.write("{ not json");
  junk.close();
  CHECK(!Album::loadFile(dir + "/junk.album", b, &err), "garbage is rejected");
  QFile after(path);
  CHECK(after.open(QIODevice::ReadOnly) && after.readAll() == before, "the saved file is untouched by failures");
}

static void singlePageMode(const QString& dir) {
  Album a;
  a.size = int(pageSizes().size()) - 2;  // A4 portrait
  CHECK(std::abs(a.spreadSize().width() - 2 * 8.27) < 1e-9, "facing: a spread is two pages wide");
  a.facing = false;
  CHECK(std::abs(a.spreadSize().width() - 8.27) < 1e-9 && std::abs(a.spreadSize().height() - 11.69) < 1e-9,
        "single page: the canvas is exactly one A4 portrait page");
  a.size = int(pageSizes().size()) - 1;  // A4 landscape
  CHECK(std::abs(a.spreadSize().width() * 25.4 - 297) < 0.5 && std::abs(a.spreadSize().height() * 25.4 - 210) < 0.5,
        "single page: A4 landscape is 297 x 210 mm");
  CHECK(std::abs(a.sheetSize().width() - (11.69 + 2 * a.bleed)) < 1e-9, "sheet adds bleed to one page only");

  const Box full = a.slotBox({0, 0, 1, 1});
  CHECK(std::abs(full.width - a.sheetSize().width()) < 1e-9, "a full-bleed slot covers the single page");

  // Round trip, and old saves (no field) stay facing.
  Album b;
  CHECK(Album::fromJson(a.toJson(), b) && !b.facing, "facing=false survives JSON");
  QJsonObject legacy = a.toJson();
  legacy.remove("facing");
  CHECK(Album::fromJson(legacy, b) && b.facing, "albums saved before the option stay facing");

  // Undo must bring the mode back too.
  const Snapshot single = a.snapshot();
  a.facing = true;
  CHECK(!(a.snapshot() == single), "facing is part of the snapshot");
  a.restore(single);
  CHECK(!a.facing, "restore brings single-page mode back");

  // One A4 page exports as one A4-plus-bleed PDF page per spread.
  PhotoRec p;
  p.id = Album::newId();
  p.path = makeJpeg(dir, "single.jpg", 1400, 1000, 30);
  p.w = 1400;
  p.h = 1000;
  p.aspect = 1.4;
  a.addPhoto(p);
  a.rebuild();
  CHECK(a.spreads.size() == 1, "one photo, one page");
  CHECK(exportPdf(a, dir + "/single.pdf").ok, "single-page export works");
  QFile f(dir + "/single.pdf");
  CHECK(f.open(QIODevice::ReadOnly), "pdf exists");
  const QByteArray data = f.readAll();
  const int at = data.indexOf("/MediaBox");
  CHECK(at >= 0, "pdf has a MediaBox");
  if (at >= 0) {
    const QList<QByteArray> nums = data.mid(at, 80).split('[').value(1).split(']').value(0).simplified().split(' ');
    const double wPt = nums.value(2).toDouble(), hPt = nums.value(3).toDouble();
    CHECK(std::abs(wPt / 72 - (11.69 + 2 * a.bleed)) < 0.02 && std::abs(hPt / 72 - (8.27 + 2 * a.bleed)) < 0.02,
          "the PDF page is one A4 landscape page plus bleed, not a double spread");
  }
}

static void historySteps() {
  auto snap = [](int bleedTenths) {
    Snapshot s;
    s.bleed = bleedTenths / 10.0;
    return s;
  };
  History h(3);
  h.reset(snap(0));
  CHECK(!h.canUndo() && !h.canRedo(), "fresh history has nothing to undo");
  CHECK(h.undo() == nullptr && h.redo() == nullptr, "stepping an empty history is a no-op");

  h.record(snap(1), "", 0);
  h.record(snap(2), "", 10);
  CHECK(h.canUndo(), "edits are undoable");
  CHECK(h.undo()->bleed == 0.1, "undo returns the previous state");
  CHECK(h.canRedo() && h.redo()->bleed == 0.2, "redo returns the undone state");

  h.undo();
  h.record(snap(5), "", 20);
  CHECK(!h.canRedo(), "a new edit discards the redo branch");

  h.record(snap(5), "", 30);
  h.undo();
  CHECK(h.undo()->bleed == 0.0, "recording an unchanged state adds no step");

  // Same-kind edits in quick succession are one step; a pause or another kind starts a new one.
  h.reset(snap(0));
  h.record(snap(1), "zoom", 1000);
  h.record(snap(2), "zoom", 1100);
  h.record(snap(3), "zoom", 1200);
  CHECK(h.undo()->bleed == 0.0 && !h.canUndo(), "a burst of zoom steps undoes in one go");
  h.redo();
  h.record(snap(4), "zoom", 1200 + History::kMergeMs + 1);
  CHECK(h.undo()->bleed == 0.3, "a pause splits the burst");
  h.record(snap(6), "pan", 5000);
  h.record(snap(7), "zoom", 5100);
  CHECK(h.undo()->bleed == 0.6, "different kinds never merge");

  // Capacity drops the oldest steps, not the newest.
  h.reset(snap(0));
  for (int i = 1; i <= 5; i++) h.record(snap(i), "", i * 10);
  int steps = 0;
  double oldest = 0;
  while (const Snapshot* s = h.undo()) {
    steps++;
    oldest = s->bleed;
  }
  CHECK(steps == 3 && oldest == 0.2, "capacity keeps the most recent steps");
}

static void snapshotRestoresEverything(const QString& dir) {
  Album a;
  for (int i = 0; i < 4; i++) {
    PhotoRec p;
    p.id = Album::newId();
    p.path = makeJpeg(dir, QString("s%1.jpg").arg(i), 300, 200, i * 60);
    p.w = 300;
    p.h = 200;
    p.aspect = 1.5;
    a.addPhoto(p);
  }
  a.rebuild();
  const Snapshot before = a.snapshot();
  const std::string gone = a.photos[1].id;
  a.removePhoto(gone);
  a.size = 5;
  a.bleed = 0.4;
  a.spreads[0].items[0].zoom = 3;
  CHECK(!(a.snapshot() == before), "edits change the snapshot");
  CHECK(a.photo(gone) == nullptr, "removed photo is gone from the index");
  a.restore(before);
  CHECK(a.snapshot() == before, "restore returns the exact state");
  CHECK(a.photo(gone) != nullptr, "photo index is rebuilt on restore");
  CHECK(a.findByPath(a.photos[1].path) != nullptr, "path index is rebuilt on restore");
}

static void probeHonoursExifOrientation(const QString& dir) {
  const QString plain = makeJpeg(dir, "plain.jpg", 400, 200, 10);
  const QString rotated = makeJpeg(dir, "rot.jpg", 400, 200, 10, QImageIOHandler::TransformationRotate90);
  const auto a = probeImage(plain), b = probeImage(rotated);
  CHECK(a.ok && a.w == 400 && a.h == 200, "plain probe");
  CHECK(b.ok && b.w == 200 && b.h == 400, "probe reports oriented size");

  // The scaled decode must agree with the probe, or aspect/cover maths would be wrong.
  ImageCache cache;
  CHECK(loadFullImage(rotated).size() == QSize(200, 400), "full decode is oriented");
  CHECK(!probeImage(dir + "/missing.jpg").ok, "missing file is not ok");
}

static void jsonRoundTrip(const QString& dir) {
  Album a;
  a.size = 4;
  a.bleed = 0.25;
  for (int i = 0; i < 5; i++) {
    PhotoRec p;
    p.id = Album::newId();
    p.path = makeJpeg(dir, QString("r%1.jpg").arg(i), 300, 200 + i * 20, i * 50);
    p.w = 300;
    p.h = 200 + i * 20;
    p.aspect = double(p.w) / p.h;
    a.addPhoto(p);
  }
  a.rebuild();
  a.spreads[0].items[0].zoom = 2.5;
  a.spreads[0].items[0].pos[0] = 20;

  Album b;
  const QJsonDocument doc = QJsonDocument::fromJson(QJsonDocument(a.toJson()).toJson());
  CHECK(Album::fromJson(doc.object(), b), "fromJson accepts our own output");
  CHECK(b.size == 4 && b.bleed == 0.25, "settings survive");
  CHECK(b.photos.size() == 5 && b.spreads.size() == a.spreads.size(), "counts survive");
  CHECK(b.spreads[0].items[0].zoom == 2.5 && b.spreads[0].items[0].pos[0] == 20, "framing survives");
  CHECK(b.photo(a.photos[3].id) && b.photo(a.photos[3].id)->exists, "photo index rebuilt");

  // Removing a photo drops its slot, and empties vanish.
  const std::size_t before = b.photos.size();
  b.removePhoto(a.photos[0].id);
  CHECK(b.photos.size() == before - 1, "photo removed");
  for (const auto& s : b.spreads)
    for (const auto& it : s.items) CHECK(it.pid != a.photos[0].id, "no dangling reference");
  CHECK(!Album::fromJson(QJsonObject{}, b), "garbage json rejected");
}

static void slotBoxPushesIntoBleed() {
  Album a;  // 12x12 pages, bleed .125, gutter .15
  const Box full = a.slotBox({0, 0, 1, 1});
  CHECK(std::abs(full.left) < 1e-9 && std::abs(full.top) < 1e-9, "full-bleed slot starts at the sheet corner");
  CHECK(std::abs(full.width - a.sheetSize().width()) < 1e-9, "full-bleed slot spans the sheet");
  const Box half = a.slotBox({0.5, 0, 0.5, 1});
  CHECK(std::abs(half.left - (a.bleed + 12 + a.gutter / 2)) < 1e-9, "inner edge gets half a gutter");
}

static void pdfExport(const QString& dir) {
  Album a;
  for (int i = 0; i < 6; i++) {
    PhotoRec p;
    p.id = Album::newId();
    p.path = makeJpeg(dir, QString("e%1.jpg").arg(i), 800, i % 2 ? 1200 : 500, i * 40);
    const auto info = probeImage(p.path);
    p.w = info.w;
    p.h = info.h;
    p.aspect = double(p.w) / p.h;
    a.addPhoto(p);
  }
  a.perSpread = 3;
  a.rebuild();
  CHECK(a.spreads.size() == 2, "two spreads of three");
  a.spreads[0].items[0].zoom = 2;

  int calls = 0;
  const QString out = dir + "/out.pdf";
  const auto r = exportPdf(a, out, [&](int done, int total) {
    calls++;
    return done <= total;
  });
  CHECK(r.ok, qPrintable(r.error));
  CHECK(calls == 2 && r.skipped == 0, "progress per spread, nothing skipped");

  QFile f(out);
  CHECK(f.open(QIODevice::ReadOnly), "pdf exists");
  const QByteArray data = f.readAll();
  CHECK(data.startsWith("%PDF"), "pdf header");
  CHECK(data.count("/Type /Page\n") + data.count("/Type /Page ") >= 2, "one page per spread");
  CHECK(data.size() > 4000, "images were embedded");

  CHECK(!exportPdf(a, out, [](int, int) { return false; }).ok, "cancel is reported");
  Album empty;
  CHECK(!exportPdf(empty, out).ok, "empty album refuses to export");
}

static void cacheTiers(const QString& dir) {
  const QString path = makeJpeg(dir, "big.jpg", 3000, 2000, 90);
  ImageCache cache;
  int arrivals = 0;
  QObject::connect(&cache, &ImageCache::loaded, [&] { arrivals++; });
  CHECK(cache.get(path, 200).isNull(), "first request is async");
  for (int i = 0; i < 200 && arrivals == 0; i++) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(10);
  }
  const QPixmap thumb = cache.get(path, 200);
  CHECK(!thumb.isNull() && std::max(thumb.width(), thumb.height()) == 256, "thumbnail tier is 256px");
  CHECK(thumb.height() < thumb.width() && std::abs(thumb.width() * 2.0 / 3 - thumb.height()) < 2, "aspect kept");

  // A scaled decode of an EXIF-rotated file must come out oriented (portrait), not sideways.
  const QString rot = makeJpeg(dir, "bigrot.jpg", 3000, 2000, 200, QImageIOHandler::TransformationRotate90);
  arrivals = 0;
  cache.get(rot, 200);
  for (int i = 0; i < 200 && arrivals == 0; i++) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(10);
  }
  const QPixmap rt = cache.get(rot, 200);
  CHECK(!rt.isNull() && rt.height() == 256 && rt.width() < rt.height(), "rotated thumbnail is portrait");
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  QTemporaryDir tmp;
  CHECK(tmp.isValid(), "temp dir");
  historySteps();
  projectFiles(tmp.path());
  singlePageMode(tmp.path());
  snapshotRestoresEverything(tmp.path());
  probeHonoursExifOrientation(tmp.path());
  exifRotationIsConsistent(tmp.path());
  jsonRoundTrip(tmp.path());
  slotBoxPushesIntoBleed();
  pdfExport(tmp.path());
  cacheTiers(tmp.path());
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("core: all checks passed\n");
  return 0;
}
