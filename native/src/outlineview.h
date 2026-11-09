// Right panel: miniature of every spread. Click to jump, drag to reorder.
#pragma once

#include <QAbstractScrollArea>

#include "album.h"
#include "imagecache.h"

namespace album::ui {

class OutlineView : public QAbstractScrollArea {
  Q_OBJECT
 public:
  OutlineView(Album* album, ImageCache* cache, QWidget* parent = nullptr);
  void reload();

 signals:
  void spreadClicked(int si);
  void reordered();

 protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  bool viewportEvent(QEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void dragEnterEvent(QDragEnterEvent*) override;
  void dragMoveEvent(QDragMoveEvent*) override;
  void dragLeaveEvent(QDragLeaveEvent*) override;
  void dropEvent(QDropEvent*) override;

 private:
  static constexpr int kPad = 8, kGap = 8;
  double miniWidth() const { return viewport()->width() - 2.0 * kPad - 8; }
  double itemHeight() const;
  QRectF itemRect(int si) const;
  int indexAt(const QPoint& vp) const;
  void updateScroll();

  Album* album_;
  ImageCache* cache_;
  int hover_ = -1, pressed_ = -1, dropTarget_ = -1;
  QPoint pressPos_;
};

}  // namespace album::ui
