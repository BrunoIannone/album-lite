// Left panel: every imported photo as a thumbnail you can drag onto a slot.
#pragma once

#include <QAbstractScrollArea>

#include "album.h"
#include "imagecache.h"

namespace album::ui {

class TrayView : public QAbstractScrollArea {
  Q_OBJECT
 public:
  TrayView(Album* album, ImageCache* cache, QWidget* parent = nullptr);
  void reload();

 signals:
  void removeRequested(const QString& photoId);

 protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  bool viewportEvent(QEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;

 private:
  static constexpr int kPad = 8, kGap = 6, kCols = 2;
  double cellSize() const;
  QRectF cellRect(int i) const;
  static QRectF removeRect(const QRectF& cell) { return {cell.right() - 24, cell.top() + 3, 21, 18}; }
  int indexAt(const QPoint& vp) const;
  void updateScroll();

  Album* album_;
  ImageCache* cache_;
  int hover_ = -1, pressed_ = -1;
  bool pressedOnRemove_ = false;
  QPoint pressPos_;
};

}  // namespace album::ui
