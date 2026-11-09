// Pure layout data + heuristics. No Qt, no I/O: a line-for-line port of ../../layout.js.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace album {

struct PageSize {
  const char* name;
  double w, h;  // inches, one page
};

inline const std::array<PageSize, 9>& pageSizes() {
  // New sizes go at the end: saved albums store an index into this list.
  static const std::array<PageSize, 9> sizes{{
      {"8 x 8 in", 8, 8},
      {"10 x 10 in", 10, 10},
      {"12 x 12 in", 12, 12},
      {"8.5 x 11 in", 8.5, 11},
      {"11 x 8.5 in", 11, 8.5},
      {"12 x 9 in", 12, 9},
      {"10 x 7 in", 10, 7},
      {"A4 portrait", 8.27, 11.69},
      {"A4 landscape", 11.69, 8.27},
  }};
  return sizes;
}

// A slot, as fractions of the SPREAD (two facing pages).
struct Rect {
  double x, y, w, h;
};
using Layout = std::vector<Rect>;

constexpr int kMaxPerSpread = 6;
constexpr double kMinDpi = 150;
constexpr double kMaxZoom = 8;

namespace detail {

inline Layout cols(int n) {
  Layout out;
  for (int i = 0; i < n; i++) out.push_back({double(i) / n, 0, 1.0 / n, 1});
  return out;
}

inline Layout grid(int c, int rows) {
  Layout out;
  for (int i = 0; i < c * rows; i++)
    out.push_back({double(i % c) / c, double(i / c) / rows, 1.0 / c, 1.0 / rows});
  return out;
}

inline std::vector<std::vector<Layout>> buildTemplates() {
  // Index n holds the layouts for n photos; photos fill slots in order, so chronology
  // survives auto-layout. Index 0 is unused.
  std::vector<std::vector<Layout>> t(kMaxPerSpread + 1);
  t[1] = {
      {{0, 0, 1, 1}},
      {{0.1, 0.1, 0.8, 0.8}},
      {{0.34, 0.05, 0.32, 0.9}},
      {{0.05, 0.26, 0.9, 0.48}},
  };
  t[2] = {
      {{0, 0, 0.5, 1}, {0.5, 0, 0.5, 1}},
      {{0.05, 0.12, 0.43, 0.76}, {0.52, 0.12, 0.43, 0.76}},
      {{0.16, 0.05, 0.28, 0.9}, {0.56, 0.05, 0.28, 0.9}},
      {{0.05, 0.06, 0.9, 0.41}, {0.05, 0.53, 0.9, 0.41}},
      {{0, 0, 0.62, 1}, {0.68, 0.22, 0.26, 0.56}},
      {{0.06, 0.22, 0.26, 0.56}, {0.38, 0, 0.62, 1}},
  };
  t[3] = {
      {{0, 0, 0.5, 1}, {0.5, 0, 0.5, 0.5}, {0.5, 0.5, 0.5, 0.5}},
      {{0, 0, 0.5, 0.5}, {0, 0.5, 0.5, 0.5}, {0.5, 0, 0.5, 1}},
      cols(3),
      {{0.06, 0.08, 0.26, 0.84}, {0.37, 0.08, 0.26, 0.84}, {0.68, 0.08, 0.26, 0.84}},
      {{0.04, 0.05, 0.92, 0.46}, {0.04, 0.55, 0.45, 0.4}, {0.51, 0.55, 0.45, 0.4}},
  };
  t[4] = {
      grid(2, 2),
      {{0.05, 0.07, 0.43, 0.41}, {0.52, 0.07, 0.43, 0.41}, {0.05, 0.52, 0.43, 0.41}, {0.52, 0.52, 0.43, 0.41}},
      cols(4),
      {{0, 0, 0.55, 1}, {0.55, 0, 0.45, 1.0 / 3}, {0.55, 1.0 / 3, 0.45, 1.0 / 3}, {0.55, 2.0 / 3, 0.45, 1.0 / 3}},
      {{0, 0, 0.45, 1.0 / 3}, {0, 1.0 / 3, 0.45, 1.0 / 3}, {0, 2.0 / 3, 0.45, 1.0 / 3}, {0.45, 0, 0.55, 1}},
  };
  t[5] = {
      {{0, 0, 0.5, 1}, {0.5, 0, 0.25, 0.5}, {0.75, 0, 0.25, 0.5}, {0.5, 0.5, 0.25, 0.5}, {0.75, 0.5, 0.25, 0.5}},
      cols(5),
      {{0.03, 0.05, 0.94, 0.45}, {0.03, 0.53, 0.225, 0.42}, {0.2675, 0.53, 0.225, 0.42}, {0.505, 0.53, 0.225, 0.42}, {0.7425, 0.53, 0.225, 0.42}},
      {{0.03, 0.05, 0.46, 0.44}, {0.51, 0.05, 0.46, 0.44}, {0.03, 0.53, 0.3, 0.42}, {0.35, 0.53, 0.3, 0.42}, {0.67, 0.53, 0.3, 0.42}},
  };
  t[6] = {
      grid(3, 2),
      cols(6),
      grid(2, 3),
      {{0.09, 0.05, 0.2, 0.44}, {0.4, 0.05, 0.2, 0.44}, {0.71, 0.05, 0.2, 0.44},
       {0.09, 0.53, 0.2, 0.42}, {0.4, 0.53, 0.2, 0.42}, {0.71, 0.53, 0.2, 0.42}},
  };
  return t;
}

}  // namespace detail

/// All layouts for `count` photos (empty if count is out of range).
inline const std::vector<Layout>& templatesFor(std::size_t count) {
  static const auto all = detail::buildTemplates();
  static const std::vector<Layout> none;
  return count >= 1 && count < all.size() ? all[count] : none;
}

inline double slotAspect(const Rect& r, double W, double H) { return (r.w * W) / (r.h * H); }

struct CoverFit {
  double fw, fh;
};

/// Multipliers (>= 1) that grow a slot-sized box until an image of this aspect covers it.
/// The grown box has exactly the image's aspect, so one multiplier is always 1.
inline CoverFit coverFit(double imgAspect, double slotAspect) {
  return {std::max(1.0, imgAspect / slotAspect), std::max(1.0, slotAspect / imgAspect)};
}

/// Print resolution once a photo is fitted and zoomed into a slot, in dots per inch.
inline double fitDpi(double pxWide, double slotWidthIn, double fitW, double zoom) {
  return pxWide / (slotWidthIn * fitW * zoom);
}

/// How badly slot shapes disagree with the photos that will land in them, in reading
/// order. Log-ratio so 2:1 too wide costs the same as 2:1 too tall.
inline double layoutCost(const Layout& layout, const std::vector<double>& aspects, double W, double H) {
  double c = 0;
  for (std::size_t i = 0; i < layout.size(); i++)
    c += std::abs(std::log(slotAspect(layout[i], W, H)) - std::log(aspects[i]));
  return c;
}

/// Index into templatesFor(n) of the best-fitting layout for these photo aspect ratios.
inline int pickTemplate(const std::vector<double>& aspects, double W, double H) {
  const auto& opts = templatesFor(aspects.size());
  int best = 0;
  double bestCost = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < opts.size(); i++) {
    const double c = layoutCost(opts[i], aspects, W, H);
    if (c < bestCost) {
      bestCost = c;
      best = int(i);
    }
  }
  return best;
}

struct Photo {
  std::string id;
  double aspect = 1;
};

/// One photo placed in a slot. An empty pid is an empty slot. pos is the share of the
/// overflow panned to, 0..100 per axis; zoom multiplies the cover fit.
struct Item {
  std::string pid;
  double pos[2] = {50, 50};
  double zoom = 1;
  bool operator==(const Item&) const = default;
};

struct Spread {
  int tpl = 0;
  std::vector<Item> items;
  bool operator==(const Spread&) const = default;
};

/// Chunk photos into spreads. Panoramas get a spread to themselves.
inline std::vector<std::vector<Photo>> autoFlow(const std::vector<Photo>& photos, int perSpread) {
  const int max = std::min(std::max(perSpread, 1), kMaxPerSpread);
  std::vector<std::vector<Photo>> spreads;
  std::vector<Photo> cur;
  auto flush = [&] {
    if (!cur.empty()) spreads.push_back(std::move(cur));
    cur.clear();
  };
  for (const auto& p : photos) {
    if (p.aspect >= 2.2 || p.aspect <= 0.45) {
      flush();
      spreads.push_back({p});
      continue;
    }
    cur.push_back(p);
    if (int(cur.size()) >= max) flush();
  }
  flush();
  return spreads;
}

/// Chunk photos into spreads, each with the best-fitting template.
inline std::vector<Spread> flowSpreads(const std::vector<Photo>& photos, int perSpread, double W, double H) {
  std::vector<Spread> out;
  for (const auto& group : autoFlow(photos, perSpread)) {
    std::vector<double> aspects;
    Spread s;
    for (const auto& p : group) {
      aspects.push_back(p.aspect);
      Item it;
      it.pid = p.id;
      s.items.push_back(it);
    }
    s.tpl = pickTemplate(aspects, W, H);
    out.push_back(std::move(s));
  }
  return out;
}

}  // namespace album
