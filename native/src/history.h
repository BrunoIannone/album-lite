// Undo/redo over whole-album snapshots. The album is small plain data (a few hundred KB even
// for thousands of photos), so snapshots beat per-action commands: every edit path is covered
// without each one having to know how to reverse itself.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "album.h"

namespace album {

class History {
 public:
  explicit History(std::size_t capacity = 100) : cap_(capacity) {}

  /// Start from `s` with nothing to undo.
  void reset(Snapshot s) {
    current_ = std::move(s);
    undo_.clear();
    redo_.clear();
    kind_.clear();
  }

  /// Call after every edit with the album's new state. Edits with the same non-empty `kind`
  /// closer together than kMergeMs (a drag, a burst of wheel zoom, typing in a box) fold
  /// into one undo step. No-op edits are ignored.
  void record(Snapshot now, const std::string& kind, std::int64_t nowMs) {
    if (now == current_) return;
    const bool merge = !kind.empty() && kind == kind_ && nowMs - at_ < kMergeMs;
    if (!merge) {
      undo_.push_back(std::move(current_));
      if (undo_.size() > cap_) undo_.erase(undo_.begin());
    }
    redo_.clear();
    current_ = std::move(now);
    kind_ = kind;
    at_ = nowMs;
  }

  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }

  /// The state to show after undo/redo, or nullptr if there is nothing to step to.
  const Snapshot* undo() {
    if (undo_.empty()) return nullptr;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    kind_.clear();
    return &current_;
  }
  const Snapshot* redo() {
    if (redo_.empty()) return nullptr;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    kind_.clear();
    return &current_;
  }

  static constexpr std::int64_t kMergeMs = 600;

 private:
  std::size_t cap_;
  Snapshot current_;
  std::vector<Snapshot> undo_, redo_;
  std::string kind_;
  std::int64_t at_ = 0;
};

}  // namespace album
