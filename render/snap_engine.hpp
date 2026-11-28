// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "camera2d.hpp"
#include "strokes.hpp"
#include <planedraft/document.hpp>

namespace pd {
struct SnapHit {
    Vec2 point;
    SnapType type;
    EntityId id, other; // other == 0 unless intersecting
    double pixels;
};
class SnapEngine {
  public:
    static constexpr auto modes = SnapType::Endpoint | SnapType::Midpoint | SnapType::Center |
                                  SnapType::Intersection | SnapType::Nearest;
    explicit SnapEngine(TextStroker text = nullptr) : text_(text) {}
    [[nodiscard]] std::optional<SnapHit> find(const Document&, const Camera2D&, Vec2 screen,
                                              double radiusPixels, SnapType mask = modes,
                                              EntityId exclude = 0) const;
    // Object picking also accepts the interior of a filled area; it is not an OSNAP feature.
    [[nodiscard]] std::optional<SnapHit> pick(const Document&, const Camera2D&, Vec2 screen,
                                              double radiusPixels) const;

  private:
    std::optional<SnapHit> search(const Document&, const Camera2D&, Vec2, double, SnapType, EntityId,
                                  bool pickFill) const;
    TextStroker text_;
};
} // namespace pd
