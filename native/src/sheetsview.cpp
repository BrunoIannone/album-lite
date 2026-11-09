#include "sheetsview.h"

#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScrollBar>
#include <QUrl>
#include <cmath>

#include "ui_common.h"

namespace album::ui {

namespace {
constexpr double kMargin = 20, kHeaderH = 28, kGap = 26;
}

SheetsView::SheetsView(Album* album, ImageCache* cache, QWidget* parent)
    : QAbstractScrollArea(parent), album_(album), cache_(cache) {
  setFrameShape(QFrame::NoFrame);
  viewport()->setMouseTracking(true);
  viewport()->setAcceptDrops(true);
  verticalScrollBar()->setSingleStep(48);
  horizontalScrollBar()->setRange(0, 0);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  connect(cache_, &ImageCache::loaded, viewport(), qOverload<>(&QWidget::update));
  autoScroll_.setInterval(30);
  connect(&autoScroll_, &QTimer::timeout, this, &SheetsView::autoScrollTick);
}

/* --- geometry ------------------------------------------------------------ */

SheetsView::Metrics SheetsView::metrics() const {
  const QSizeF sheet = album_->sheetSize();
  const double fit = (viewport()->width() - 44) / (sheet.width() * 96);
  const double ppi = std::max(8.0, 96 * std::min(1.0, fit));
  const double sw = sheet.width() * ppi, sh = sheet.height() * ppi;
  return {ppi, (viewport()->width() - sw) / 2, sw, sh, kHeaderH + sh + kGap};
}

double SheetsView::cellTop(int si, const Metrics& m) const { return kMargin + si * m.cell; }

QRectF SheetsView::slotRect(int si, int ii, const Metrics& m) const {
  const Spread& sp = album_->spreads[std::size_t(si)];
  const album::Box b = album_->slotBox(album_->templateOf(sp)[std::size_t(ii)]);
  const double top = cellTop(si, m) + kHeaderH;
  return {m.left + b.left * m.ppi, top + b.top * m.ppi, b.width * m.ppi, b.height * m.ppi};
}

std::vector<SheetsView::HeaderItem> SheetsView::headerItems(int si, const Metrics& m) const {
  const QFontMetricsF fm(font());
  const Spread& sp = album_->spreads[std::size_t(si)];
  const int n = int(templatesFor(sp.items.size()).size());
  std::vector<HeaderItem> out;
  double x = m.left;
  const double y = cellTop(si, m) + 3, h = 22;
  auto text = [&](const QString& t) {
    const double w = fm.horizontalAdvance(t);
    out.push_back({QRectF(x, y, w, h), -1, t});
    x += w + 8;
  };
  auto button = [&](int kind, const QString& t) {
    const double w = fm.horizontalAdvance(t) + 16;
    out.push_back({QRectF(x, y, w, h), kind, t});
    x += w + 8;
  };
  text(QString(album_->facing ? "Spread %1" : "Page %1").arg(si + 1));
  button(Prev, "◀");
  text(QString("layout %1/%2").arg(sp.tpl + 1).arg(n));
  button(Next, "▶");
  button(Fit, "Best fit");
  button(AddPhoto, "+ Photo");
  button(AddSpread, album_->facing ? "+ Spread after" : "+ Page after");
  button(Delete, album_->facing ? "Delete spread" : "Delete page");
  return out;
}

SheetsView::SlotUi SheetsView::slotUi(const QRectF& slot, bool hasPhoto) {
  constexpr double bh = 20, gap = 3, margin = 4;
  double x = slot.right() - margin;
  auto take = [&](double w) {
    x -= w;
    const QRectF r(x, slot.top() + margin, w, bh);
    x -= gap;
    return r;
  };
  SlotUi u;
  u.remove = take(22);
  u.grab = take(22);
  if (hasPhoto) {
    u.zoomIn = take(22);
    u.readout = take(42);
    u.zoomOut = take(22);
  }
  return u;
}

const PhotoRec* SheetsView::photoAt(const Hit& h) const {
  if (h.si < 0 || h.ii < 0 || std::size_t(h.si) >= album_->spreads.size()) return nullptr;
  const Spread& sp = album_->spreads[std::size_t(h.si)];
  if (std::size_t(h.ii) >= sp.items.size()) return nullptr;
  const PhotoRec* p = album_->photo(sp.items[std::size_t(h.ii)].pid);
  return p && p->exists ? p : nullptr;
}

SheetsView::Hit SheetsView::hitTest(const QPoint& vp) const {
  const Metrics m = metrics();
  const double y = vp.y() + verticalScrollBar()->value();
  const int n = int(album_->spreads.size());
  if (y < kMargin || n == 0) return {};
  const int si = int((y - kMargin) / m.cell);
  if (si >= n) return {};
  const QPointF pt(vp.x(), y);
  const double local = y - cellTop(si, m);

  if (local < kHeaderH) {
    for (const auto& it : headerItems(si, m))
      if (it.kind >= 0 && it.r.contains(pt)) return {Part::Header, si, -1, it.kind};
    return {};
  }
  if (local > kHeaderH + m.sheetH) return {};

  const Spread& sp = album_->spreads[std::size_t(si)];
  const std::size_t slotCount = std::min(album_->templateOf(sp).size(), sp.items.size());
  for (std::size_t ii = 0; ii < slotCount; ii++) {
    const QRectF slot = slotRect(si, int(ii), m);
    if (!slot.contains(pt)) continue;
    const bool hasPhoto = photoAt({Part::Slot, si, int(ii), -1}) != nullptr;
    const SlotUi ui = slotUi(slot, hasPhoto);
    Hit h{Part::Slot, si, int(ii), -1};
    if (ui.remove.contains(pt)) h.part = Part::Remove;
    else if (ui.grab.contains(pt)) h.part = Part::Grab;
    else if (hasPhoto && ui.zoomIn.contains(pt)) h.part = Part::ZoomIn;
    else if (hasPhoto && ui.zoomOut.contains(pt)) h.part = Part::ZoomOut;
    return h;
  }
  return {};
}

void SheetsView::updateScroll() {
  const Metrics m = metrics();
  const int n = int(album_->spreads.size());
  const double total = n ? 2 * kMargin + n * m.cell - kGap : 0;
  verticalScrollBar()->setRange(0, std::max(0, int(std::ceil(total - viewport()->height()))));
  verticalScrollBar()->setPageStep(viewport()->height());
}

void SheetsView::reload() {
  hover_ = press_ = dropTarget_ = {};
  pan_ = {};
  updateScroll();
  viewport()->update();
}

void SheetsView::scrollToSpread(int si) {
  if (si < 0 || std::size_t(si) >= album_->spreads.size()) return;
  verticalScrollBar()->setValue(int(cellTop(si, metrics()) - 6));
}

void SheetsView::resizeEvent(QResizeEvent* e) {
  QAbstractScrollArea::resizeEvent(e);
  updateScroll();
}

QPoint SheetsView::headerButtonPos(int si, int btn) const {
  const int scroll = verticalScrollBar()->value();
  for (const auto& it : headerItems(si, metrics()))
    if (it.kind == btn) return QPoint(int(it.r.center().x()), int(it.r.center().y()) - scroll);
  return {};
}

QPoint SheetsView::slotCenterPos(int si, int ii) const {
  const QPointF c = slotRect(si, ii, metrics()).center();
  return QPoint(int(c.x()), int(c.y()) - verticalScrollBar()->value());
}

/* --- painting ------------------------------------------------------------ */

void SheetsView::paintEvent(QPaintEvent* e) {
  QPainter p(viewport());
  p.fillRect(e->rect(), kBg);
  p.setRenderHint(QPainter::Antialiasing);

  const int n = int(album_->spreads.size());
  if (n == 0) {
    p.setPen(kMuted);
    p.drawText(viewport()->rect(), Qt::AlignCenter,
               "Import photos to begin\n(Import button, or drop image files here)");
    return;
  }
  const Metrics m = metrics();
  const int scroll = verticalScrollBar()->value();
  p.translate(0, -scroll);
  const int first = std::max(0, int((scroll - kMargin) / m.cell));
  const int last = std::min(n - 1, int((scroll + viewport()->height() - kMargin) / m.cell));
  for (int si = first; si <= last; si++) paintSpread(p, si, m);
}

void SheetsView::paintSpread(QPainter& p, int si, const Metrics& m) {
  // Header: label + buttons.
  p.setPen(kMuted);
  for (const auto& it : headerItems(si, m)) {
    if (it.kind < 0) {
      p.drawText(it.r, Qt::AlignVCenter | Qt::AlignLeft, it.text);
      continue;
    }
    const bool hot = hover_.part == Part::Header && hover_.si == si && hover_.btn == it.kind;
    p.setBrush(hot ? kButtonHover : kButton);
    p.setPen(kLine);
    p.drawRoundedRect(it.r, 4, 4);
    p.setPen(QColor("#e8e8ea"));
    p.drawText(it.r, Qt::AlignCenter, it.text);
  }

  const QRectF sheet(m.left, cellTop(si, m) + kHeaderH, m.sheetW, m.sheetH);
  for (int i = 5; i >= 1; i--) {  // soft drop shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 22));
    p.drawRoundedRect(sheet.adjusted(-i * 2, 0, i * 2, 0).translated(0, 4 + i), 3, 3);
  }
  p.fillRect(sheet, Qt::white);

  const Spread& sp = album_->spreads[std::size_t(si)];
  const std::size_t slotCount = std::min(album_->templateOf(sp).size(), sp.items.size());
  for (std::size_t ii = 0; ii < slotCount; ii++) paintSlot(p, si, int(ii), slotRect(si, int(ii), m));

  if (album_->facing) {
    p.setPen(QPen(QColor(0, 0, 0, 34), 1, Qt::DashLine));  // spine hint
    p.drawLine(QPointF(sheet.center().x(), sheet.top()), QPointF(sheet.center().x(), sheet.bottom()));
  }

  if (hover_.si == si && isSlotHit(hover_)) {
    const QRectF slot = slotRect(si, hover_.ii, m);
    paintSlotUi(p, hover_, slot);
  }
}

void SheetsView::paintSlot(QPainter& p, int si, int ii, const QRectF& slot) {
  const Item& item = album_->spreads[std::size_t(si)].items[std::size_t(ii)];
  const PhotoRec* ph = photoAt({Part::Slot, si, ii, -1});
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#eceff3"));
  p.drawRect(slot);

  if (ph) {
    const QRectF img = coverRect(ph->aspect, slot, item.zoom, item.pos);
    const int want = int(std::max(img.width(), img.height()) * devicePixelRatioF());
    const QPixmap pm = cache_->get(ph->path, want);
    if (!pm.isNull()) {
      p.save();
      p.setClipRect(slot);
      p.setRenderHint(QPainter::SmoothPixmapTransform);
      p.drawPixmap(img, pm, QRectF(pm.rect()));
      p.restore();
    }
    if (ph->w) {
      const album::Box box = album_->slotBox(album_->templateOf(album_->spreads[std::size_t(si)])[std::size_t(ii)]);
      const CoverFit f = coverFit(ph->aspect, box.width / box.height);
      const double dpi = fitDpi(ph->w, box.width, f.fw, item.zoom);
      if (dpi < kMinDpi) {
        const QString t = QString("%1 DPI").arg(int(std::lround(dpi)));
        const QRectF r(slot.left() + 6, slot.bottom() - 24, QFontMetricsF(font()).horizontalAdvance(t) + 12, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xb3, 0x26, 0x1e, 0xd9));
        p.drawRoundedRect(r, 4, 4);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, t);
      }
    }
  } else {
    p.setPen(QPen(QColor("#b4bbc6"), 2, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(slot.adjusted(4, 4, -4, -4));
    const PhotoRec* any = album_->photo(item.pid);
    p.setPen(QColor("#8b93a0"));
    p.drawText(slot.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWrapAnywhere,
               any ? Album::label(*any) + "\n(file not found)" : "Drop a photo here");
  }

  if (dropTarget_.si == si && dropTarget_.ii == ii) {
    p.setPen(QPen(kAccent, 3));
    p.setBrush(Qt::NoBrush);
    p.drawRect(slot.adjusted(1.5, 1.5, -1.5, -1.5));
  }
}

void SheetsView::paintSlotUi(QPainter& p, const Hit& h, const QRectF& slot) {
  const bool hasPhoto = photoAt(h) != nullptr;
  const SlotUi ui = slotUi(slot, hasPhoto);
  auto chip = [&](const QRectF& r, const QString& t, bool hot) {
    p.setPen(Qt::NoPen);
    p.setBrush(hot ? QColor(60, 60, 60, 220) : QColor(0, 0, 0, 170));
    p.drawRoundedRect(r, 4, 4);
    p.setPen(Qt::white);
    p.drawText(r, Qt::AlignCenter, t);
  };
  chip(ui.remove, "✕", h.part == Part::Remove);
  chip(ui.grab, "⠿", h.part == Part::Grab);
  if (hasPhoto) {
    const Item& it = album_->spreads[std::size_t(h.si)].items[std::size_t(h.ii)];
    chip(ui.zoomIn, "+", h.part == Part::ZoomIn);
    chip(ui.readout, QString("%1%").arg(int(std::lround(it.zoom * 100))), false);
    chip(ui.zoomOut, "−", h.part == Part::ZoomOut);
  }
}

/* --- interaction --------------------------------------------------------- */

bool SheetsView::viewportEvent(QEvent* e) {
  if (e->type() == QEvent::Leave) {
    hover_ = {};
    viewport()->update();
  }
  return QAbstractScrollArea::viewportEvent(e);
}

void SheetsView::updateHover(const QPoint& pos) {
  const Hit h = hitTest(pos);
  if (!(h == hover_)) {
    hover_ = h;
    viewport()->update();
  }
  Qt::CursorShape cur = Qt::ArrowCursor;
  if (h.part == Part::Slot && photoAt(h)) cur = Qt::OpenHandCursor;
  else if (h.part != Part::None && h.part != Part::Slot) cur = Qt::PointingHandCursor;
  viewport()->setCursor(cur);
}

void SheetsView::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  press_ = hitTest(e->pos());
  pressPos_ = e->pos();
  if (press_.part == Part::Slot && photoAt(press_)) {
    // Overflow is read off the geometry at press time, so panning is right at any zoom.
    const Metrics m = metrics();
    const QRectF slot = slotRect(press_.si, press_.ii, m);
    const Item& it = album_->spreads[std::size_t(press_.si)].items[std::size_t(press_.ii)];
    const QRectF img = coverRect(photoAt(press_)->aspect, slot, it.zoom, it.pos);
    pan_ = {true, false, press_.si, press_.ii, e->position(), {it.pos[0], it.pos[1]},
            img.width() - slot.width(), img.height() - slot.height()};
    viewport()->setCursor(Qt::ClosedHandCursor);
  }
}

void SheetsView::mouseMoveEvent(QMouseEvent* e) {
  if (pan_.active) {
    Item& it = album_->spreads[std::size_t(pan_.si)].items[std::size_t(pan_.ii)];
    const QPointF d = e->position() - pan_.start;
    if (pan_.ovx > 0.5) it.pos[0] = std::clamp(pan_.pos0[0] - d.x() / pan_.ovx * 100, 0.0, 100.0);
    if (pan_.ovy > 0.5) it.pos[1] = std::clamp(pan_.pos0[1] - d.y() / pan_.ovy * 100, 0.0, 100.0);
    pan_.moved = true;
    viewport()->update();
    return;
  }
  if ((e->buttons() & Qt::LeftButton) && press_.part == Part::Grab &&
      (e->pos() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
    const Hit h = press_;
    press_ = {};
    startDrag(h);
    return;
  }
  updateHover(e->pos());
}

void SheetsView::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  if (pan_.active) {
    const bool moved = pan_.moved;
    pan_ = {};
    press_ = {};
    updateHover(e->pos());
    if (moved) emit framingChanged();
    return;
  }
  const Hit h = hitTest(e->pos());
  const Hit pressed = press_;
  press_ = {};
  if (!(h == pressed)) return;
  switch (h.part) {
    case Part::Header: headerAction(h); break;
    case Part::Remove: slotAction(h); break;
    case Part::ZoomIn: zoomBy(h, 1.25); break;
    case Part::ZoomOut: zoomBy(h, 1 / 1.25); break;
    default: break;
  }
}

void SheetsView::mouseDoubleClickEvent(QMouseEvent* e) {
  const Hit h = hitTest(e->pos());
  if (h.part != Part::Slot || !photoAt(h)) return;
  Item& it = album_->spreads[std::size_t(h.si)].items[std::size_t(h.ii)];
  it.pos[0] = it.pos[1] = 50;
  it.zoom = 1;
  viewport()->update();
  emit framingChanged();
}

void SheetsView::wheelEvent(QWheelEvent* e) {
  if (e->modifiers() & Qt::ControlModifier) {
    const Hit h = hitTest(e->position().toPoint());
    if (isSlotHit(h) && photoAt(h)) {
      zoomBy(h, std::exp(e->angleDelta().y() / 300.0));
      e->accept();
      return;
    }
  }
  QAbstractScrollArea::wheelEvent(e);
}

void SheetsView::zoomBy(const Hit& h, double factor) {
  Item& it = album_->spreads[std::size_t(h.si)].items[std::size_t(h.ii)];
  it.zoom = std::clamp(it.zoom * factor, 1.0, kMaxZoom);
  viewport()->update();
  emit framingChanged();
}

void SheetsView::headerAction(const Hit& h) {
  auto& spreads = album_->spreads;
  Spread& sp = spreads[std::size_t(h.si)];
  const int n = int(templatesFor(sp.items.size()).size());
  switch (h.btn) {
    case Prev: sp.tpl = (sp.tpl + n - 1) % n; break;
    case Next: sp.tpl = (sp.tpl + 1) % n; break;
    case Fit: album_->refit(sp); break;
    case AddPhoto:
      if (sp.items.size() >= std::size_t(kMaxPerSpread)) return;
      sp.items.push_back(Item{});
      album_->refit(sp);
      break;
    case AddSpread: spreads.insert(spreads.begin() + h.si + 1, Spread{0, {Item{}}}); break;
    case Delete: spreads.erase(spreads.begin() + h.si); break;
    default: return;
  }
  reload();
  emit albumChanged();
}

void SheetsView::slotAction(const Hit& h) {
  auto& spreads = album_->spreads;
  Spread& sp = spreads[std::size_t(h.si)];
  sp.items.erase(sp.items.begin() + h.ii);
  if (sp.items.empty()) spreads.erase(spreads.begin() + h.si);
  else album_->refit(sp);
  reload();
  emit albumChanged();
}

/* --- drag & drop --------------------------------------------------------- */

void SheetsView::startDrag(const Hit& h) {
  auto* mime = new QMimeData;
  mime->setData(kSlotMime, QString("%1:%2").arg(h.si).arg(h.ii).toUtf8());
  auto* drag = new QDrag(this);
  drag->setMimeData(mime);
  if (const PhotoRec* p = photoAt(h)) {
    const QPixmap pm = cache_->get(p->path, 256);
    if (!pm.isNull()) drag->setPixmap(pm.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  drag->exec(Qt::MoveAction);
  hover_ = {};
  viewport()->update();
}

void SheetsView::dragEnterEvent(QDragEnterEvent* e) {
  const QMimeData* m = e->mimeData();
  if (m->hasFormat(kPhotoMime) || m->hasFormat(kSlotMime) || m->hasUrls()) e->acceptProposedAction();
  autoScroll_.start();
}

void SheetsView::dragMoveEvent(QDragMoveEvent* e) {
  dragPos_ = e->position().toPoint();
  const QMimeData* m = e->mimeData();
  const Hit h = hitTest(dragPos_);
  const Hit target = isSlotHit(h) ? Hit{Part::Slot, h.si, h.ii, -1} : Hit{};
  if (!(target == dropTarget_)) {
    dropTarget_ = target;
    viewport()->update();
  }
  if (m->hasUrls() || (target.part != Part::None && (m->hasFormat(kPhotoMime) || m->hasFormat(kSlotMime))))
    e->acceptProposedAction();
  else
    e->ignore();
}

void SheetsView::dragLeaveEvent(QDragLeaveEvent*) {
  dropTarget_ = {};
  autoScroll_.stop();
  viewport()->update();
}

void SheetsView::autoScrollTick() {
  const int edge = 48;
  auto* bar = verticalScrollBar();
  if (dragPos_.y() < edge) bar->setValue(bar->value() - 24);
  else if (dragPos_.y() > viewport()->height() - edge) bar->setValue(bar->value() + 24);
}

void SheetsView::dropEvent(QDropEvent* e) {
  autoScroll_.stop();
  const QMimeData* m = e->mimeData();
  const Hit target = dropTarget_;
  dropTarget_ = {};

  if (m->hasUrls() && !m->hasFormat(kPhotoMime) && !m->hasFormat(kSlotMime)) {
    QStringList paths;
    for (const QUrl& u : m->urls())
      if (u.isLocalFile()) paths << u.toLocalFile();
    e->acceptProposedAction();
    if (!paths.isEmpty()) emit filesDropped(paths);
    return;
  }
  if (target.part == Part::None || std::size_t(target.si) >= album_->spreads.size()) return;
  Item& item = album_->spreads[std::size_t(target.si)].items[std::size_t(target.ii)];

  if (m->hasFormat(kPhotoMime)) {
    const std::string id = QString::fromUtf8(m->data(kPhotoMime)).toStdString();
    if (!album_->photo(id)) return;
    item.pid = id;
    item.pos[0] = item.pos[1] = 50;
    item.zoom = 1;
  } else if (m->hasFormat(kSlotMime)) {
    const QStringList parts = QString::fromUtf8(m->data(kSlotMime)).split(':');
    const int si = parts.value(0).toInt(), ii = parts.value(1).toInt();
    if (si < 0 || std::size_t(si) >= album_->spreads.size()) return;
    auto& items = album_->spreads[std::size_t(si)].items;
    if (ii < 0 || std::size_t(ii) >= items.size()) return;
    // Framing belongs to the photo, so it travels with it.
    std::swap(item, items[std::size_t(ii)]);
  } else {
    return;
  }
  e->acceptProposedAction();
  reload();
  emit albumChanged();
}

}  // namespace album::ui
