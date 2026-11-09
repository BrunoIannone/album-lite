#include "album.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>

namespace album {

Box Album::slotBox(const Rect& rect) const {
  const QSizeF s = spreadSize();
  const double W = s.width(), H = s.height(), b = bleed;
  const bool L = rect.x <= 0.001, T = rect.y <= 0.001;
  const bool R = rect.x + rect.w >= 0.999, B = rect.y + rect.h >= 0.999;
  const double g = gutter / 2;
  const double pt = T ? 0 : g, pr = R ? 0 : g, pb = B ? 0 : g, pl = L ? 0 : g;
  return {b + rect.x * W - (L ? b : 0) + pl,
          b + rect.y * H - (T ? b : 0) + pt,
          rect.w * W + (L ? b : 0) + (R ? b : 0) - pl - pr,
          rect.h * H + (T ? b : 0) + (B ? b : 0) - pt - pb};
}

const Layout& Album::templateOf(const Spread& s) const {
  static const Layout none;
  const auto& opts = templatesFor(s.items.size());
  if (opts.empty()) return none;
  return s.tpl >= 0 && std::size_t(s.tpl) < opts.size() ? opts[std::size_t(s.tpl)] : opts[0];
}

const PhotoRec* Album::photo(const std::string& id) const {
  const auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &photos[it->second];
}

PhotoRec* Album::photo(const std::string& id) {
  return const_cast<PhotoRec*>(std::as_const(*this).photo(id));
}

PhotoRec* Album::findByPath(const QString& path) {
  const auto it = byPath_.constFind(path);
  return it == byPath_.cend() ? nullptr : &photos[it.value()];
}

PhotoRec& Album::addPhoto(PhotoRec p) {
  photos.push_back(std::move(p));
  byId_[photos.back().id] = photos.size() - 1;
  byPath_[photos.back().path] = photos.size() - 1;
  return photos.back();
}

void Album::reindex() {
  byId_.clear();
  byPath_.clear();
  for (std::size_t i = 0; i < photos.size(); i++) {
    byId_[photos[i].id] = i;
    byPath_[photos[i].path] = i;
  }
}

void Album::removePhoto(const std::string& id) {
  std::erase_if(photos, [&](const PhotoRec& p) { return p.id == id; });
  reindex();
  std::vector<Spread> kept;
  for (auto& sp : spreads) {
    const auto before = sp.items.size();
    std::erase_if(sp.items, [&](const Item& it) { return it.pid == id; });
    if (sp.items.empty()) continue;
    if (sp.items.size() != before) refit(sp);
    kept.push_back(std::move(sp));
  }
  spreads = std::move(kept);
}

void Album::clear() {
  photos.clear();
  spreads.clear();
  reindex();
}

void Album::refit(Spread& s) const {
  const QSizeF sz = spreadSize();
  std::vector<double> aspects;
  for (const auto& it : s.items) {
    const PhotoRec* p = photo(it.pid);
    aspects.push_back(p ? p->aspect : 1.0);
  }
  s.tpl = pickTemplate(aspects, sz.width(), sz.height());
}

void Album::refitAll() {
  for (auto& s : spreads) refit(s);
}

void Album::rebuild() {
  std::vector<Photo> flat;
  for (const auto& p : photos) flat.push_back({p.id, p.aspect});
  const QSizeF sz = spreadSize();
  spreads = flowSpreads(flat, perSpread, sz.width(), sz.height());
}

std::unordered_set<std::string> Album::usedIds() const {
  std::unordered_set<std::string> used;
  for (const auto& s : spreads)
    for (const auto& it : s.items)
      if (!it.pid.empty()) used.insert(it.pid);
  return used;
}

std::vector<LowRes> Album::lowRes() const {
  std::vector<LowRes> out;
  for (const auto& sp : spreads) {
    const Layout& layout = templateOf(sp);
    for (std::size_t i = 0; i < layout.size() && i < sp.items.size(); i++) {
      const PhotoRec* p = photo(sp.items[i].pid);
      if (!p || !p->w) continue;
      const Box box = slotBox(layout[i]);
      const CoverFit f = coverFit(p->aspect, box.width / box.height);
      const double dpi = fitDpi(p->w, box.width, f.fw, sp.items[i].zoom);
      if (dpi < kMinDpi) out.push_back({label(*p), int(std::lround(dpi))});
    }
  }
  return out;
}

QString Album::label(const PhotoRec& p) { return QFileInfo(p.path).fileName(); }

std::string Album::newId() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8).toStdString();
}

QJsonObject Album::toJson() const {
  QJsonArray ph;
  for (const auto& p : photos)
    ph.append(QJsonObject{{"id", QString::fromStdString(p.id)}, {"key", p.path},
                          {"aspect", p.aspect}, {"w", p.w}, {"h", p.h}});
  QJsonArray sp;
  for (const auto& s : spreads) {
    QJsonArray items;
    for (const auto& it : s.items)
      items.append(QJsonObject{
          {"pid", it.pid.empty() ? QJsonValue(QJsonValue::Null) : QJsonValue(QString::fromStdString(it.pid))},
          {"pos", QJsonArray{it.pos[0], it.pos[1]}},
          {"zoom", it.zoom}});
    sp.append(QJsonObject{{"tpl", s.tpl}, {"items", items}});
  }
  return {{"version", kFormatVersion}, {"size", size}, {"bleed", bleed}, {"gutter", gutter},
          {"perSpread", perSpread}, {"facing", facing}, {"photos", ph}, {"spreads", sp}};
}

bool Album::saveFile(const QString& path, QString* error) const {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    if (error) *error = f.errorString();
    return false;
  }
  f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
  if (!f.commit()) {
    if (error) *error = f.errorString();
    return false;
  }
  return true;
}

bool Album::loadFile(const QString& path, Album& out, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = f.errorString();
    return false;
  }
  QJsonParseError parse;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject() || !fromJson(doc.object(), out)) {
    if (error) *error = "Not a valid album project file.";
    return false;
  }
  return true;
}

bool Album::fromJson(const QJsonObject& o, Album& out) {
  if (!o.contains("photos") || !o.contains("spreads")) return false;
  Album a;
  a.loadedVersion = o["version"].toInt(1);
  a.size = std::clamp(o["size"].toInt(a.size), 0, int(pageSizes().size()) - 1);
  a.bleed = std::max(0.0, o["bleed"].toDouble(a.bleed));
  a.gutter = std::max(0.0, o["gutter"].toDouble(a.gutter));
  a.perSpread = std::clamp(o["perSpread"].toInt(a.perSpread), 1, kMaxPerSpread);
  a.facing = o["facing"].toBool(true);  // albums saved before this option are facing pages

  for (const auto& v : o["photos"].toArray()) {
    const QJsonObject p = v.toObject();
    PhotoRec r;
    r.id = p["id"].toString().toStdString();
    r.path = p["key"].toString();
    r.aspect = p["aspect"].toDouble(1);
    if (r.aspect <= 0) r.aspect = 1;
    r.w = p["w"].toInt();
    r.h = p["h"].toInt();
    if (r.id.empty() || r.path.isEmpty()) continue;
    r.exists = QFileInfo::exists(r.path);
    a.photos.push_back(std::move(r));
  }
  a.reindex();

  for (const auto& v : o["spreads"].toArray()) {
    const QJsonObject s = v.toObject();
    Spread sp;
    sp.tpl = s["tpl"].toInt();
    for (const auto& iv : s["items"].toArray()) {
      const QJsonObject io = iv.toObject();
      Item it;
      it.pid = io["pid"].isString() ? io["pid"].toString().toStdString() : std::string();
      if (!it.pid.empty() && !a.photo(it.pid)) it.pid.clear();
      const QJsonArray pos = io["pos"].toArray();
      if (pos.size() == 2) {
        it.pos[0] = std::clamp(pos[0].toDouble(50), 0.0, 100.0);
        it.pos[1] = std::clamp(pos[1].toDouble(50), 0.0, 100.0);
      }
      it.zoom = std::clamp(io["zoom"].toDouble(1), 1.0, kMaxZoom);
      sp.items.push_back(it);
    }
    if (sp.items.empty() || sp.items.size() > std::size_t(kMaxPerSpread)) continue;
    sp.tpl = std::clamp(sp.tpl, 0, int(templatesFor(sp.items.size()).size()) - 1);
    a.spreads.push_back(std::move(sp));
  }
  out = std::move(a);
  return true;
}

}  // namespace album
