#include "imagecache.h"

#include <QImageReader>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#include <algorithm>

namespace album {

namespace {
// Longest-side sizes in pixels; 0 means the original.
constexpr int kTiers[] = {256, 1024, 2560, 0};
constexpr int kTierCount = 4;
constexpr int kCacheKb = 384 * 1024;

// For EXIF-rotated files (typical camera JPEGs) QImageReader::size() and setScaledSize()
// both speak in *stored* pixels, while read() with auto-transform returns *rotated* pixels.
struct Dims {
  QSize stored;    // as laid out in the file
  QSize oriented;  // as displayed, after EXIF rotation
};

Dims readDims(const QString& path) {
  QImageReader r(path);
  r.setAutoTransform(false);
  Dims d;
  d.stored = r.size();
  d.oriented = d.stored;
  if (d.stored.isValid() && (int(r.transformation()) & int(QImageIOHandler::TransformationRotate90)))
    d.oriented = d.stored.transposed();
  return d;
}

QImage decode(const QString& path, int side) {
  QImageReader r(path);
  r.setAutoTransform(true);
  if (side > 0) {
    QSize sz = readDims(path).stored;
    if (sz.isValid() && std::max(sz.width(), sz.height()) > side) {
      sz.scale(side, side, Qt::KeepAspectRatio);
      r.setScaledSize(sz);
    }
  }
  return r.read();
}
}  // namespace

ProbeResult probeImage(const QString& path) {
  ProbeResult out{path, false, 0, 0};
  QSize sz = readDims(path).oriented;
  if (!sz.isValid()) {  // formats that can't report a size up front
    QImageReader r(path);
    r.setAutoTransform(true);
    sz = r.read().size();
  }
  if (sz.isValid() && !sz.isEmpty()) {
    out.ok = true;
    out.w = sz.width();
    out.h = sz.height();
  }
  return out;
}

QImage loadFullImage(const QString& path) { return decode(path, 0); }

ImageCache::ImageCache(QObject* parent) : QObject(parent), cache_(kCacheKb) {
  pool_.setMaxThreadCount(std::clamp(QThread::idealThreadCount(), 2, 6));
}

ImageCache::~ImageCache() {
  pool_.clear();
  pool_.waitForDone();
}

int ImageCache::tierIndexFor(int wantSide) {
  if (wantSide <= 0) return kTierCount - 1;
  for (int i = 0; i < kTierCount - 1; i++)
    if (wantSide <= kTiers[i]) return i;
  return kTierCount - 1;
}

QPixmap ImageCache::get(const QString& path, int wantSide) {
  const int want = tierIndexFor(wantSide);
  if (const QPixmap* exact = cache_.object(key(path, want))) return *exact;
  request(path, want);

  // Prefer a sharper tier already in memory, otherwise the nearest blurrier one.
  for (int i = want + 1; i < kTierCount; i++)
    if (const QPixmap* pm = cache_.object(key(path, i))) return *pm;
  for (int i = want - 1; i >= 0; i--)
    if (const QPixmap* pm = cache_.object(key(path, i))) return *pm;
  return {};
}

void ImageCache::request(const QString& path, int tierIdx) {
  const QString k = key(path, tierIdx);
  if (inflight_.contains(k) || failed_.contains(k)) return;
  inflight_.insert(k);
  const int side = kTiers[tierIdx];
  // Newest request gets the highest priority, so what is on screen now loads first.
  pool_.start(QRunnable::create([this, path, k, side] {
    QImage img = decode(path, side);
    QMetaObject::invokeMethod(this, [this, k, img] {
      inflight_.remove(k);
      if (img.isNull()) {
        failed_.insert(k);
        return;
      }
      const int cost = int(std::max<qsizetype>(1, img.sizeInBytes() / 1024));
      cache_.insert(k, new QPixmap(QPixmap::fromImage(img)), cost);
      emit loaded();
    }, Qt::QueuedConnection);
  }), ++priority_);
}

void ImageCache::forget(const QString& path) {
  for (int i = 0; i < kTierCount; i++) {
    cache_.remove(key(path, i));
    failed_.remove(key(path, i));
  }
}

void ImageCache::clear() {
  cache_.clear();
  failed_.clear();
}

}  // namespace album
