#include "trayview.h"

#include <QApplication>
#include <QDrag>
#include <QHelpEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainterPath>
#include <QScrollBar>
#include <QToolTip>
#include <cmath>

#include "ui_common.h"

namespace album::ui {

TrayView::TrayView(Album* album, ImageCache* cache, QWidget* parent)
    : QAbstractScrollArea(parent), album_(album), cache_(cache) {
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setMouseTracking(true);
  verticalScrollBar()->setSingleStep(40);
  connect(cache_, &ImageCache::loaded, viewport(), qOverload<>(&QWidget::update));
}

double TrayView::cellSize() const {
  return std::max(24.0, (viewport()->width() - 2.0 * kPad - (kCols - 1) * kGap) / kCols);
}

QRectF TrayView::cellRect(int i) const {
  const double c = cellSize();
  return {kPad + (i % kCols) * (c + kGap), kPad + (i / kCols) * (c + kGap), c, c};
}

int TrayView::indexAt(const QPoint& vp) const {
  const QPointF pt(vp.x(), vp.y() + verticalScrollBar()->value());
  const double c = cellSize();
  const int row = int((pt.y() - kPad) / (c + kGap));
  const int col = int((pt.x() - kPad) / (c + kGap));
  if (pt.y() < kPad || pt.x() < kPad || col < 0 || col >= kCols) return -1;
  const int i = row * kCols + col;
  return i >= 0 && std::size_t(i) < album_->photos.size() && cellRect(i).contains(pt) ? i : -1;
}

void TrayView::updateScroll() {
  const int n = int(album_->photos.size());
  const int rows = (n + kCols - 1) / kCols;
  const double total = rows ? 2 * kPad + rows * cellSize() + (rows - 1) * kGap : 0;
  verticalScrollBar()->setRange(0, std::max(0, int(std::ceil(total - viewport()->height()))));
  verticalScrollBar()->setPageStep(viewport()->height());
}

void TrayView::reload() {
  hover_ = pressed_ = -1;
  updateScroll();
  viewport()->update();
}

void TrayView::resizeEvent(QResizeEvent* e) {
  QAbstractScrollArea::resizeEvent(e);
  updateScroll();
}

void TrayView::paintEvent(QPaintEvent* e) {
  QPainter p(viewport());
  p.fillRect(e->rect(), kPanel);
  p.setRenderHint(QPainter::Antialiasing);
  const int n = int(album_->photos.size());
  if (n == 0) {
    p.setPen(kMuted);
    p.drawText(viewport()->rect().adjusted(kPad, kPad, -kPad, -kPad), Qt::AlignTop | Qt::TextWordWrap,
               "Import photos to begin. Drag a thumbnail onto any slot to replace it.");
    return;
  }

  const int scroll = verticalScrollBar()->value();
  const double c = cellSize();
  p.translate(0, -scroll);
  const int firstRow = std::max(0, int((scroll - kPad) / (c + kGap)));
  const int lastRow = int((scroll + viewport()->height()) / (c + kGap));
  const auto used = album_->usedIds();
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  for (int i = firstRow * kCols; i < std::min(n, (lastRow + 1) * kCols); i++) {
    const PhotoRec& ph = album_->photos[std::size_t(i)];
    const QRectF r = cellRect(i);
    if (ph.exists) {
      p.save();
      QPainterPath clip;
      clip.addRoundedRect(r, 4, 4);
      p.setClipPath(clip);
      p.fillRect(r, QColor("#3a3e45"));
      drawCover(p, cache_->get(ph.path, int(c * devicePixelRatioF())), r);
      if (!used.count(ph.id)) {
        const QRectF tag(r.left(), r.bottom() - 18, r.width(), 18);
        p.fillRect(tag, QColor(0xb8, 0x86, 0x0b, 0xe6));
        p.setPen(Qt::white);
        QFont f = font();
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        p.drawText(tag, Qt::AlignCenter, "unused");
      }
      p.restore();
    } else {
      p.setPen(QPen(kLine, 1, Qt::DashLine));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(r, 4, 4);
      p.setPen(kMuted);
      p.drawText(r.adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWrapAnywhere, Album::label(ph));
    }
    if (hover_ == i) {
      const QRectF x = removeRect(r);
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(0, 0, 0, 170));
      p.drawRoundedRect(x, 4, 4);
      p.setPen(Qt::white);
      p.drawText(x, Qt::AlignCenter, "✕");
    }
  }
}

bool TrayView::viewportEvent(QEvent* e) {
  if (e->type() == QEvent::Leave) {
    hover_ = -1;
    viewport()->update();
  } else if (e->type() == QEvent::ToolTip) {
    const auto* h = static_cast<QHelpEvent*>(e);
    const int i = indexAt(h->pos());
    if (i >= 0) {
      const PhotoRec& ph = album_->photos[std::size_t(i)];
      QToolTip::showText(h->globalPos(),
                         ph.exists ? ph.path : Album::label(ph) + " — file not found, re-import it to restore it");
    } else {
      QToolTip::hideText();
    }
    return true;
  }
  return QAbstractScrollArea::viewportEvent(e);
}

void TrayView::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  pressed_ = indexAt(e->pos());
  pressPos_ = e->pos();
  pressedOnRemove_ = pressed_ >= 0 &&
      removeRect(cellRect(pressed_).translated(0, -verticalScrollBar()->value())).contains(e->position());
}

void TrayView::mouseMoveEvent(QMouseEvent* e) {
  if ((e->buttons() & Qt::LeftButton) && pressed_ >= 0 && !pressedOnRemove_ &&
      (e->pos() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
    const PhotoRec& ph = album_->photos[std::size_t(pressed_)];
    pressed_ = -1;
    if (!ph.exists) return;
    auto* mime = new QMimeData;
    mime->setData(kPhotoMime, QByteArray::fromStdString(ph.id));
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    const QPixmap pm = cache_->get(ph.path, 256);
    if (!pm.isNull()) drag->setPixmap(pm.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    drag->exec(Qt::CopyAction);
    return;
  }
  const int i = indexAt(e->pos());
  if (i != hover_) {
    hover_ = i;
    viewport()->update();
  }
}

void TrayView::mouseReleaseEvent(QMouseEvent* e) {
  const int i = indexAt(e->pos());
  if (e->button() == Qt::LeftButton && pressedOnRemove_ && i == pressed_ && i >= 0 &&
      removeRect(cellRect(i).translated(0, -verticalScrollBar()->value())).contains(e->position())) {
    const QString id = QString::fromStdString(album_->photos[std::size_t(i)].id);
    pressed_ = -1;
    pressedOnRemove_ = false;
    emit removeRequested(id);
    return;
  }
  pressed_ = -1;
  pressedOnRemove_ = false;
}

}  // namespace album::ui
