#include "outlineview.h"

#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScrollBar>
#include <cmath>

#include "ui_common.h"

namespace album::ui {

OutlineView::OutlineView(Album* album, ImageCache* cache, QWidget* parent)
    : QAbstractScrollArea(parent), album_(album), cache_(cache) {
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setMouseTracking(true);
  viewport()->setAcceptDrops(true);
  verticalScrollBar()->setSingleStep(40);
  connect(cache_, &ImageCache::loaded, viewport(), qOverload<>(&QWidget::update));
}

double OutlineView::itemHeight() const {
  const QSizeF s = album_->spreadSize();
  return miniWidth() * s.height() / s.width() + 8;
}

QRectF OutlineView::itemRect(int si) const {
  return {double(kPad), kPad + si * (itemHeight() + kGap), viewport()->width() - 2.0 * kPad, itemHeight()};
}

int OutlineView::indexAt(const QPoint& vp) const {
  const QPointF pt(vp.x(), vp.y() + verticalScrollBar()->value());
  const int si = int((pt.y() - kPad) / (itemHeight() + kGap));
  return pt.y() >= kPad && si >= 0 && std::size_t(si) < album_->spreads.size() && itemRect(si).contains(pt) ? si : -1;
}

void OutlineView::updateScroll() {
  const int n = int(album_->spreads.size());
  const double total = n ? 2 * kPad + n * itemHeight() + (n - 1) * kGap : 0;
  verticalScrollBar()->setRange(0, std::max(0, int(std::ceil(total - viewport()->height()))));
  verticalScrollBar()->setPageStep(viewport()->height());
}

void OutlineView::reload() {
  hover_ = pressed_ = dropTarget_ = -1;
  updateScroll();
  viewport()->update();
}

void OutlineView::resizeEvent(QResizeEvent* e) {
  QAbstractScrollArea::resizeEvent(e);
  updateScroll();
}

void OutlineView::paintEvent(QPaintEvent* e) {
  QPainter p(viewport());
  p.fillRect(e->rect(), kPanel);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  const int n = int(album_->spreads.size());
  const int scroll = verticalScrollBar()->value();
  p.translate(0, -scroll);
  const double step = itemHeight() + kGap;
  const int first = std::max(0, int((scroll - kPad) / step));
  const int last = std::min(n - 1, int((scroll + viewport()->height()) / step));

  for (int si = first; si <= last; si++) {
    const QRectF box = itemRect(si);
    p.setPen(dropTarget_ == si ? QPen(kAccent, 2) : QPen(kLine, 1));
    p.setBrush(hover_ == si ? kButton : Qt::NoBrush);
    p.drawRoundedRect(box, 5, 5);

    const QRectF mini = box.adjusted(4, 4, -4, -4);
    p.fillRect(mini, Qt::white);
    const Spread& sp = album_->spreads[std::size_t(si)];
    const Layout& layout = album_->templateOf(sp);
    for (std::size_t ii = 0; ii < layout.size() && ii < sp.items.size(); ii++) {
      const Rect& r = layout[ii];
      const QRectF cell(mini.left() + r.x * mini.width(), mini.top() + r.y * mini.height(),
                        r.w * mini.width(), r.h * mini.height());
      p.fillRect(cell, QColor("#cfd4db"));
      const PhotoRec* ph = album_->photo(sp.items[ii].pid);
      if (!ph || !ph->exists) continue;
      p.save();
      p.setClipRect(cell);
      drawCover(p, cache_->get(ph->path, int(std::max(cell.width(), cell.height()) * devicePixelRatioF())), cell);
      p.restore();
    }

    const QString num = QString::number(si + 1);
    const QRectF tag(box.left() + 8, box.top() + 6, QFontMetricsF(font()).horizontalAdvance(num) + 8, 15);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(tag, 3, 3);
    p.setPen(Qt::white);
    p.drawText(tag, Qt::AlignCenter, num);
  }
}

bool OutlineView::viewportEvent(QEvent* e) {
  if (e->type() == QEvent::Leave) {
    hover_ = -1;
    viewport()->update();
  }
  return QAbstractScrollArea::viewportEvent(e);
}

void OutlineView::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  pressed_ = indexAt(e->pos());
  pressPos_ = e->pos();
}

void OutlineView::mouseMoveEvent(QMouseEvent* e) {
  if ((e->buttons() & Qt::LeftButton) && pressed_ >= 0 &&
      (e->pos() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
    const int si = pressed_;
    pressed_ = -1;
    auto* mime = new QMimeData;
    mime->setData(kSpreadMime, QByteArray::number(si));
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
    return;
  }
  const int i = indexAt(e->pos());
  if (i != hover_) {
    hover_ = i;
    viewport()->update();
  }
}

void OutlineView::mouseReleaseEvent(QMouseEvent* e) {
  const int i = indexAt(e->pos());
  const int pressed = pressed_;
  pressed_ = -1;
  if (e->button() == Qt::LeftButton && i >= 0 && i == pressed) emit spreadClicked(i);
}

void OutlineView::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasFormat(kSpreadMime)) e->acceptProposedAction();
}

void OutlineView::dragMoveEvent(QDragMoveEvent* e) {
  const int i = indexAt(e->position().toPoint());
  if (i != dropTarget_) {
    dropTarget_ = i;
    viewport()->update();
  }
  if (i >= 0 && e->mimeData()->hasFormat(kSpreadMime)) e->acceptProposedAction();
  else e->ignore();
}

void OutlineView::dragLeaveEvent(QDragLeaveEvent*) {
  dropTarget_ = -1;
  viewport()->update();
}

void OutlineView::dropEvent(QDropEvent* e) {
  const int to = dropTarget_;
  dropTarget_ = -1;
  const int from = e->mimeData()->data(kSpreadMime).toInt();
  auto& spreads = album_->spreads;
  if (to < 0 || from < 0 || std::size_t(from) >= spreads.size() || std::size_t(to) >= spreads.size()) return;
  Spread moved = std::move(spreads[std::size_t(from)]);
  spreads.erase(spreads.begin() + from);
  spreads.insert(spreads.begin() + to, std::move(moved));
  e->acceptProposedAction();
  emit reordered();
}

}  // namespace album::ui
