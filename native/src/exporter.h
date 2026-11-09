// Print-ready PDF: one page per spread, sized to trim + bleed, photos at source resolution.
#pragma once

#include <QString>
#include <functional>

#include "album.h"

namespace album {

struct ExportResult {
  bool ok = false;
  QString error;
  int skipped = 0;  // photos whose file could not be read
};

/// progress(done, total) is called after each spread; return false to cancel.
ExportResult exportPdf(const Album& album, const QString& path,
                       const std::function<bool(int, int)>& progress = {});

}  // namespace album
