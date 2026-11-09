// The main canvas: every spread stacked vertically. Only the spreads in view are painted,
// and photos are decoded on demand at the size the screen needs.
#pragma once

#include <QAbstractScrollArea>
#include <QTimer>

#include "album.h"
#include "imagecache.h"

namespace album::ui {

class SheetsView : public QAbstractScrollArea {
  Q_OBJECT
 public:
  SheetsView(Album* album, ImageCache* cache, QWidget* parent = nullptr);

  /// Call after the model changed from outside.
  void reload();
  void scrollToSpread(int si);

  /// Viewport positions of a header button (SheetsView::Btn order: prev, next, fit,
  /// add photo, add spread, delete) and of a slot's centre; used by UI tests.
  QPoint headerButtonPos(int si, int btn) const;
  QPoint slotCenterPos(int si, int ii) const;

 signals:
  void albumChanged();                    // structure or content changed
  void framingChanged();                  // only pan/zoom changed
  void filesDropped(const QStringList& paths);

 protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  bool viewportEvent(QEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void dragEnterEvent(QDragEnterEvent*) override;
  void dragMoveEvent(QDragMoveEvent*) override;
  void dragLeaveEvent(QDragLeaveEvent*) override;
  void dropEvent(QDropEvent*) override;

 private:
  enum class Part { None, Header, Slot, ZoomOut, ZoomIn, Grab, Remove };
  enum Btn { Prev, Next, Fit, AddPhoto, AddSpread, Delete };
  struct Hit {
    Part part = Part::None;
    int si = -1, ii = -1, btn = -1;
    bool operator==(const Hit&) const = default;
  };
  struct Metrics {
    double ppi, left, sheetW, sheetH, cell;
  };
  struct HeaderItem {
    QRectF r;
    int kind;  // -1 = plain text
    QString text;
  };
  struct SlotUi {
    QRectF zoomOut, readout, zoomIn, grab, remove;
  };
  struct Pan {
    bool active = false, moved = false;
    int si = -1, ii = -1;
    QPointF start;
    double pos0[2] = {50, 50};
    double ovx = 0, ovy = 0;
  };

  Metrics metrics() const;
  double cellTop(int si, const Metrics& m) const;
  QRectF slotRect(int si, int ii, const Metrics& m) const;
  std::vector<HeaderItem> headerItems(int si, const Metrics& m) const;
  static SlotUi slotUi(const QRectF& slot, bool hasPhoto);
  Hit hitTest(const QPoint& viewportPos) const;
  const PhotoRec* photoAt(const Hit& h) const;
  bool isSlotHit(const Hit& h) const { return h.ii >= 0 && h.part != Part::None && h.part != Part::Header; }

  void updateScroll();
  void paintSpread(QPainter& p, int si, const Metrics& m);
  void paintSlot(QPainter& p, int si, int ii, const QRectF& slot);
  void paintSlotUi(QPainter& p, const Hit& h, const QRectF& slot);
  void headerAction(const Hit& h);
  void slotAction(const Hit& h);
  void zoomBy(const Hit& h, double factor);
  void startDrag(const Hit& h);
  void updateHover(const QPoint& pos);
  void autoScrollTick();

  Album* album_;
  ImageCache* cache_;
  Hit hover_, press_, dropTarget_;
  QPoint pressPos_;
  Pan pan_;
  QTimer autoScroll_;
  QPoint dragPos_;
};

}  // namespace album::ui
