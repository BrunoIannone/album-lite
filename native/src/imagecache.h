// Asynchronous, size-tiered image cache. Views ask for "a pixmap at least N px on its
// longest side"; the best tier already in memory is returned immediately and the wanted
// tier is decoded on a worker thread (JPEGs decode straight to the reduced size).
#pragma once

#include <QCache>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QThreadPool>

namespace album {

struct ProbeResult {
  QString path;
  bool ok = false;
  int w = 0, h = 0;  // after EXIF orientation
};

/// Reads the oriented pixel size, normally from the file header without decoding.
ProbeResult probeImage(const QString& path);

/// Full-resolution, EXIF-oriented decode (used by PDF export).
QImage loadFullImage(const QString& path);

class ImageCache : public QObject {
  Q_OBJECT
 public:
  explicit ImageCache(QObject* parent = nullptr);
  ~ImageCache() override;

  /// Best available pixmap for `wantSide` device pixels (0 = full resolution).
  /// Null while nothing is decoded yet; emits loaded() when more detail arrives.
  QPixmap get(const QString& path, int wantSide);
  void forget(const QString& path);
  void clear();

 signals:
  void loaded();

 private:
  static int tierIndexFor(int wantSide);
  static QString key(const QString& path, int tier) { return path + '@' + QString::number(tier); }
  void request(const QString& path, int tierIdx);

  QCache<QString, QPixmap> cache_;
  QSet<QString> inflight_;
  QSet<QString> failed_;
  QThreadPool pool_;
  int priority_ = 0;
};

}  // namespace album
