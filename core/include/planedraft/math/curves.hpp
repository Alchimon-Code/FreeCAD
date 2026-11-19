// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <optional>
#include <planedraft/math/geometry.hpp>

namespace pd {
struct Intersection {
    enum class Kind { None, OnePoint, TwoPoints, Overlap, Coincident };
    Kind kind = Kind::None;
    Vec2 first{}, second{}; // Overlap: the two ends of the common segment.
};
Intersection intersectSegments(Vec2 a, Vec2 b, Vec2 c, Vec2 d);
Intersection intersectSegmentCircle(Vec2 a, Vec2 b, Vec2 center, double radius);
Intersection intersectCircles(Vec2 c1, double r1, Vec2 c2, double r2);
Vec2 closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b);
Vec2 closestPointOnCircle(Vec2 p, Vec2 center, double radius);

// A non-full circular arc. Positive sweep = CCW. Bulge belongs to the outgoing segment.
class CircularArc {
  public:
    CircularArc(Vec2 center, double radius, double startAngle, double sweep);
    static CircularArc fromBulge(Vec2 a, Vec2 b, double bulge);
    [[nodiscard]] Vec2 center() const { return center_; }
    [[nodiscard]] double radius() const { return radius_; }
    [[nodiscard]] double startAngle() const { return start_; }
    [[nodiscard]] double sweep() const { return sweep_; }
    [[nodiscard]] bool containsAngle(double angle) const;
    [[nodiscard]] Vec2 pointAt(double fraction) const;                     // [0,1], including exact endpoints
    [[nodiscard]] std::optional<Vec2> quadrantPoint(unsigned index) const; // +X, +Y, -X, -Y
    [[nodiscard]] Vec2 closestPoint(Vec2 p) const;
    [[nodiscard]] BoundingBox2D bounds() const;
    [[nodiscard]] CircularArc transformed(const Matrix3x3& mat) const;

  private:
    Vec2 center_, first_, last_;
    [[nodiscard]] double along(Vec2 radialDirection) const;
    [[nodiscard]] std::optional<double> fractionAt(Vec2 radialDirection) const;
    double radius_, start_, sweep_;
};
} // namespace pd
