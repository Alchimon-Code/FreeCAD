// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/math/geometry.hpp>

#include <array>

namespace pd {
// Input and sizes in logical pixels (top-left origin); world and all subtractions in binary64.
class Camera2D {
  public:
    static constexpr double minScale = 1e-8, maxScale = 1e6;
    void resize(double width, double height);
    void setView(Vec2 eye, double pixelsPerUnit);
    void pan(Vec2 screenDelta);
    void zoomAt(Vec2 screenAnchor, double factor);
    bool fit(const BoundingBox2D& extents);

    [[nodiscard]] Vec2 eye() const { return eye_; }
    [[nodiscard]] Vec2 size() const { return size_; }
    [[nodiscard]] double scale() const { return scale_; }
    [[nodiscard]] Vec2 worldToScreen(Vec2 world) const;
    [[nodiscard]] Vec2 screenToWorld(Vec2 screen) const;
    [[nodiscard]] BoundingBox2D viewBounds(double padPixels = 0) const;
    // VBO vertices are relative to the eye at upload time. Only this matrix changes during a small pan.
    [[nodiscard]] static std::array<float, 2> relativeToEye(Vec2 world, Vec2 uploadEye);
    [[nodiscard]] std::array<float, 9> clipMatrix(Vec2 uploadEye) const; // column-major for GLSL

  private:
    Vec2 eye_{}, size_{1, 1};
    double scale_ = 3;
};
} // namespace pd
