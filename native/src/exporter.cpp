#include "exporter.h"

#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

#include "imagecache.h"

namespace album {

ExportResult exportPdf(const Album& album, const QString& path,
                       const std::function<bool(int, int)>& progress) {
  ExportResult res;
  if (album.spreads.empty()) {
    res.error = "Nothing to export.";
    return res;
  }

  QPdfWriter pdf(path);
  const QSizeF sheet = album.sheetSize();
  pdf.setPageSize(QPageSize(sheet, QPageSize::Inch));
  pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
  pdf.setResolution(300);
  pdf.setTitle("Album");

  QPainter painter(&pdf);
  if (!painter.isActive()) {
    res.error = "Could not write " + path;
    return res;
  }
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  const double ppi = pdf.resolution();
  const int total = int(album.spreads.size());

  for (int si = 0; si < total; si++) {
    if (si > 0) pdf.newPage();
    painter.resetTransform();
    painter.scale(ppi, ppi);  // from here on, coordinates are inches on the sheet

    const Spread& sp = album.spreads[std::size_t(si)];
    const Layout& layout = album.templateOf(sp);
    for (std::size_t ii = 0; ii < layout.size() && ii < sp.items.size(); ii++) {
      const Item& item = sp.items[ii];
      const PhotoRec* p = album.photo(item.pid);
      if (!p) continue;

      // One full-size decode at a time keeps memory bounded however big the album is.
      const QImage img = loadFullImage(p->path);
      if (img.isNull()) {
        res.skipped++;
        continue;
      }
      const QRectF slot = album.slotBox(layout[ii]).toRect();
      painter.save();
      painter.setClipRect(slot);
      painter.drawImage(coverRect(double(img.width()) / img.height(), slot, item.zoom, item.pos), img);
      painter.restore();
    }
    if (progress && !progress(si + 1, total)) {
      painter.end();
      res.error = "Export cancelled.";
      return res;
    }
  }
  painter.end();
  res.ok = true;
  return res;
}

}  // namespace album
