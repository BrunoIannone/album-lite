// Port of ../../test.js. Plain asserts so the layout core builds and tests without Qt.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "layout.h"

using namespace album;

static int failures = 0;
#define CHECK(cond, msg)                                                         \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("FAIL %s:%d  %s  (%s)\n", __FILE__, __LINE__, msg, #cond);     \
      failures++;                                                                \
    }                                                                            \
  } while (0)

static const double W = 24, H = 12;  // a 12 x 12 in page, two facing

static void templatesStayInside() {
  for (int n = 1; n <= kMaxPerSpread; n++) {
    for (const auto& layout : templatesFor(n)) {
      CHECK(int(layout.size()) == n, "slot count matches photo count");
      for (const auto& s : layout) {
        CHECK(s.x >= 0 && s.y >= 0 && s.x + s.w <= 1.0001 && s.y + s.h <= 1.0001, "slot out of bounds");
        CHECK(s.w > 0 && s.h > 0, "slot has area");
      }
    }
  }
}

static void templatesNeverOverlap() {
  auto hits = [](const Rect& a, const Rect& b) {
    return a.x < b.x + b.w - 1e-6 && b.x < a.x + a.w - 1e-6 &&
           a.y < b.y + b.h - 1e-6 && b.y < a.y + a.h - 1e-6;
  };
  for (int n = 1; n <= kMaxPerSpread; n++)
    for (const auto& layout : templatesFor(n))
      for (std::size_t i = 0; i < layout.size(); i++)
        for (std::size_t j = i + 1; j < layout.size(); j++)
          CHECK(!hits(layout[i], layout[j]), "template slots overlap");
}

static void shapesFollowPhotos() {
  for (const auto& s : templatesFor(2)[pickTemplate({0.67, 0.67}, W, H)])
    CHECK(slotAspect(s, W, H) < 1, "portraits should get tall slots");
  for (const auto& s : templatesFor(2)[pickTemplate({3, 3}, W, H)])
    CHECK(slotAspect(s, W, H) > 2, "panoramas should get wide slots");

  CHECK(slotAspect(templatesFor(1)[pickTemplate({3.5}, W, H)][0], W, H) > 3, "lone panorama gets banner");
  CHECK(slotAspect(templatesFor(1)[pickTemplate({0.67}, W, H)][0], W, H) < 1, "lone portrait gets tall slot");

  auto mean = [](const Layout& l) {
    double sum = 0;
    for (const auto& r : l) sum += slotAspect(r, W, H);
    return sum / l.size();
  };
  for (int n = 1; n <= kMaxPerSpread; n++) {
    const double tall = mean(templatesFor(n)[pickTemplate(std::vector<double>(n, 0.67), W, H)]);
    const double wide = mean(templatesFor(n)[pickTemplate(std::vector<double>(n, 1.5), W, H)]);
    CHECK(tall < wide, "portrait pick should be narrower than landscape pick");
  }
}

static void flowKeepsOrder() {
  const std::vector<Photo> photos{{"a", 1.5}, {"b", 0.67}, {"c", 3.5}, {"d", 1}, {"e", 1}};
  const auto spreads = flowSpreads(photos, 2, W, H);
  std::string order;
  for (const auto& s : spreads) {
    CHECK(s.tpl >= 0 && s.tpl < int(templatesFor(s.items.size()).size()), "tpl indexes a real template");
    for (const auto& i : s.items) {
      order += i.pid;
      CHECK(i.pos[0] == 50 && i.pos[1] == 50 && i.zoom == 1, "default framing");
    }
  }
  CHECK(order == "abcde", "photos must stay in import order across spreads");
}

static void coverFitCovers() {
  const double slots[][2] = {{12, 12}, {24, 12}, {10, 7}, {8.5, 11}, {4, 9}};
  const double aspects[] = {0.4, 0.67, 1, 1.5, 2, 3.5};
  for (const auto& s : slots) {
    for (double a : aspects) {
      const auto f = coverFit(a, s[0] / s[1]);
      CHECK(f.fw >= 1 && f.fh >= 1, "a gap would show if either axis shrank");
      CHECK(std::abs(std::min(f.fw, f.fh) - 1) < 1e-9, "one axis must sit flush");
      CHECK(std::abs((s[0] * f.fw) / (s[1] * f.fh) - a) < 1e-9, "grown box keeps the photo aspect");
    }
  }
}

static void panNeverExposesGap() {
  for (double zoom : {1.0, 1.5, 4.0}) {
    const auto f = coverFit(1.5, 24.0 / 12);
    const double zw = f.fw * zoom, zh = f.fh * zoom;
    for (double pos : {0.0, 50.0, 100.0}) {
      const double left = -(zw - 1) * pos, top = -(zh - 1) * pos;
      CHECK(left <= 1e-9 && left >= -(zw - 1) * 100 - 1e-9, "left edge escaped the slot");
      CHECK(top <= 1e-9 && top >= -(zh - 1) * 100 - 1e-9, "top edge escaped the slot");
      CHECK(left / 100 + zw >= 1 - 1e-9, "right edge left a gap");
      CHECK(top / 100 + zh >= 1 - 1e-9, "bottom edge left a gap");
    }
  }
}

static void dpiFallsWithZoom() {
  CHECK(fitDpi(6000, 12, 1, 1) == 500, "full-width fit");
  CHECK(fitDpi(6000, 12, 1, 2) == 250, "2x zoom halves dpi");
  CHECK(fitDpi(6000, 12, 1, 4) < kMinDpi, "4x zoom trips the warning");
  CHECK(fitDpi(6000, 12, 2, 1) == 250, "cropping costs resolution");
}

static void autoFlowChunks() {
  const std::vector<Photo> photos{{"1", 1}, {"2", 1}, {"3", 3.5}, {"4", 1}, {"5", 1}, {"6", 1}};
  const auto flow = autoFlow(photos, 2);
  const std::size_t expect[] = {2, 1, 2, 1};
  CHECK(flow.size() == 4, "four spreads");
  for (std::size_t i = 0; i < flow.size() && i < 4; i++) CHECK(flow[i].size() == expect[i], "chunk size");
  CHECK(flow.size() > 1 && flow[1][0].aspect == 3.5, "panorama isolated");
}

static void a4LandscapeIsOnePage() {
  const PageSize& p = pageSizes().back();
  CHECK(std::string(p.name) == "A4 landscape", "A4 landscape is the last entry, so saved indices stay valid");
  CHECK(std::abs(p.w * 25.4 - 297) < 0.5 && std::abs(p.h * 25.4 - 210) < 0.5, "one page is 297 x 210 mm");
  CHECK(std::abs(p.w * 2 * 25.4 - 594) < 1, "a spread is two pages: 594 mm wide");
  CHECK(std::string(pageSizes()[7].name) == "A4 portrait", "earlier entries did not move");
}

int main() {
  a4LandscapeIsOnePage();
  templatesStayInside();
  templatesNeverOverlap();
  shapesFollowPhotos();
  flowKeepsOrder();
  coverFitCovers();
  panNeverExposesGap();
  dpiFallsWithZoom();
  autoFlowChunks();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("layout: all checks passed\n");
  return EXIT_SUCCESS;
}
