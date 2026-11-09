#pragma once

#include <QColor>
#include <QPainter>
#include <QPixmap>

namespace album::ui {

inline const QColor kBg("#1b1d21");
inline const QColor kPanel("#24272c");
inline const QColor kLine("#3a3e45");
inline const QColor kMuted("#9aa0aa");
inline const QColor kAccent("#6aa9ff");
inline const QColor kButton("#31353c");
inline const QColor kButtonHover("#3d434c");

/// Draw `pm` so it covers `target`, centred, cropping whatever overflows.
inline void drawCover(QPainter& p, const QPixmap& pm, const QRectF& target) {
  if (pm.isNull() || target.isEmpty()) return;
  const double ta = target.width() / target.height();
  const double pa = double(pm.width()) / pm.height();
  QRectF src(0, 0, pm.width(), pm.height());
  if (pa > ta) {
    const double w = pm.height() * ta;
    src = QRectF((pm.width() - w) / 2, 0, w, pm.height());
  } else {
    const double h = pm.width() / ta;
    src = QRectF(0, (pm.height() - h) / 2, pm.width(), h);
  }
  p.drawPixmap(target, pm, src);
}

inline constexpr const char* kPhotoMime = "application/x-album-photo";
inline constexpr const char* kSlotMime = "application/x-album-slot";
inline constexpr const char* kSpreadMime = "application/x-album-spread";

}  // namespace album::ui
