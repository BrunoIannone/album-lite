// Album model: page geometry, spreads, photo index and JSON persistence. The JSON shape
// matches the web app's saved state (photos: id/key/aspect/w/h, spreads: tpl/items).
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "layout.h"

namespace album {

struct PhotoRec {
  std::string id;
  QString path;  // absolute file path; same path is never two photos
  double aspect = 1;
  int w = 0, h = 0;
  bool exists = true;
  bool operator==(const PhotoRec&) const = default;
};

/// Everything an edit can change (not the lookup indexes, which are rebuilt on restore).
struct Snapshot {
  int size = 2;
  double bleed = 0, gutter = 0;
  int perSpread = 4;
  bool facing = true;  // false: a spread is one page, so there is no spine
  std::vector<PhotoRec> photos;
  std::vector<Spread> spreads;
  bool operator==(const Snapshot&) const = default;
};

// Slot geometry in inches, measured from the sheet's top-left corner (bleed included).
struct Box {
  double left = 0, top = 0, width = 0, height = 0;
  QRectF toRect() const { return {left, top, width, height}; }
};

struct LowRes {
  QString name;
  int dpi;
};

/// Saved-album format. 2: photo sizes are post-EXIF-rotation (v1 stored unrotated sizes).
constexpr int kFormatVersion = 2;

/// Where a photo lands inside `slot` once cover-fitted and zoomed. pos is 0..100 per axis,
/// the share of the overflow panned to. Result is in the same units as `slot`.
inline QRectF coverRect(double aspect, const QRectF& slot, double zoom, const double pos[2]) {
  const CoverFit f = coverFit(aspect, slot.width() / slot.height());
  const double zw = f.fw * zoom, zh = f.fh * zoom;
  return {slot.left() - (zw - 1) * slot.width() * pos[0] / 100.0,
          slot.top() - (zh - 1) * slot.height() * pos[1] / 100.0,
          slot.width() * zw, slot.height() * zh};
}

class Album {
 public:
  int size = 2;  // index into pageSizes()
  double bleed = 0.125;
  double gutter = 0.15;
  int perSpread = 4;
  bool facing = true;  // false: a spread is a single page and has no spine
  int loadedVersion = kFormatVersion;  // format the data was read from (older ones need repair)
  std::vector<PhotoRec> photos;
  std::vector<Spread> spreads;

  const PageSize& page() const { return pageSizes()[std::size_t(size)]; }
  QSizeF spreadSize() const { return {page().w * (facing ? 2 : 1), page().h}; }
  QSizeF sheetSize() const {
    const QSizeF s = spreadSize();
    return {s.width() + bleed * 2, s.height() + bleed * 2};
  }

  /// Slots touching a trim edge are pushed out into the bleed; the gutter is subtracted
  /// from the geometry so a slot's box is exactly the area the photo fills.
  Box slotBox(const Rect& rect) const;
  const Layout& templateOf(const Spread& s) const;

  const PhotoRec* photo(const std::string& id) const;
  PhotoRec* photo(const std::string& id);
  PhotoRec* findByPath(const QString& path);

  PhotoRec& addPhoto(PhotoRec p);
  void removePhoto(const std::string& id);
  void clear();
  void reindex();

  void refit(Spread& s) const;
  void refitAll();
  /// Re-flow every photo into fresh spreads (Auto-layout).
  void rebuild();

  std::unordered_set<std::string> usedIds() const;
  std::vector<LowRes> lowRes() const;

  QJsonObject toJson() const;
  static bool fromJson(const QJsonObject& obj, Album& out);

  /// Project files are the album JSON. Writes are atomic (a crash never leaves half a file).
  bool saveFile(const QString& path, QString* error = nullptr) const;
  static bool loadFile(const QString& path, Album& out, QString* error = nullptr);
  bool isEmpty() const { return photos.empty() && spreads.empty(); }

  Snapshot snapshot() const { return {size, bleed, gutter, perSpread, facing, photos, spreads}; }
  void restore(const Snapshot& s) {
    size = s.size;
    bleed = s.bleed;
    gutter = s.gutter;
    perSpread = s.perSpread;
    facing = s.facing;
    photos = s.photos;
    spreads = s.spreads;
    reindex();
  }

  static QString label(const PhotoRec& p);
  static std::string newId();

 private:
  std::unordered_map<std::string, std::size_t> byId_;
  QHash<QString, std::size_t> byPath_;
};

}  // namespace album
