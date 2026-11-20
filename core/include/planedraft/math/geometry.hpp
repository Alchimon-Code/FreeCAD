// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <limits>
#include <stdexcept>

namespace pd {
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53);
inline constexpr double EPSILON = 1e-9;

struct Vec2 {
    double x = 0, y = 0;
    constexpr bool operator==(const Vec2&) const = default;
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2 operator+(Vec2 p) const { return {x + p.x, y + p.y}; }
    constexpr Vec2 operator-(Vec2 p) const { return {x - p.x, y - p.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    [[nodiscard]] constexpr double dot(Vec2 p) const { return x * p.x + y * p.y; }
    [[nodiscard]] constexpr double cross(Vec2 p) const { return x * p.y - y * p.x; }
    [[nodiscard]] constexpr Vec2 leftNormal() const { return {-y, x}; }
    [[nodiscard]] bool finite() const;
    [[nodiscard]] double length() const;
    [[nodiscard]] Vec2 normalized() const; // throws for non-finite or near-zero length
};
constexpr Vec2 operator*(double s, Vec2 p) { return p * s; }
Vec2 midpoint(Vec2 a, Vec2 b);

// Closed, axis-aligned bounds. Default construction is the empty union identity.
class BoundingBox2D {
  public:
    BoundingBox2D() = default;
    BoundingBox2D(Vec2 min, Vec2 max);
    static BoundingBox2D fromPoints(Vec2 a, Vec2 b);
    [[nodiscard]] Vec2 min() const { return min_; }
    [[nodiscard]] Vec2 max() const { return max_; }
    [[nodiscard]] bool empty() const { return min_.x > max_.x; }
    [[nodiscard]] bool contains(Vec2 p) const;
    [[nodiscard]] bool contains(const BoundingBox2D& other) const;
    [[nodiscard]] bool intersects(const BoundingBox2D& other) const;
    void include(Vec2 p);
    void include(const BoundingBox2D& other);
    bool operator==(const BoundingBox2D&) const = default;

  private:
    static constexpr double inf = std::numeric_limits<double>::infinity();
    Vec2 min_{inf, inf}, max_{-inf, -inf};
};

// A caller can distinguish loss of circularity from other invalid geometry without parsing what().
struct NotSimilarity final : std::invalid_argument {
    NotSimilarity() : std::invalid_argument("circular geometry requires a similarity transform") {}
};
// Column vectors; A*B applies B first. The last row is always [0,0,1]. Angles are radians.
class Matrix3x3 {
  public:
    Matrix3x3() = default;
    Matrix3x3(double a, double b, double tx, double c, double d, double ty);
    static Matrix3x3 translation(Vec2 offset);
    static Matrix3x3 rotation(double angle);
    static Matrix3x3 scaling(double sx, double sy);
    static Matrix3x3 reflection(Vec2 pointOnAxis, Vec2 axisDirection);
    [[nodiscard]] Vec2 applyPoint(Vec2 p) const;
    [[nodiscard]] Vec2 applyVector(Vec2 v) const;
    [[nodiscard]] BoundingBox2D applyBounds(const BoundingBox2D& box) const;
    [[nodiscard]] Matrix3x3 operator*(const Matrix3x3& rhs) const;
    [[nodiscard]] bool invertible() const;
    // Magnitude is the uniform scale. Negative means orientation reversing. Throws for shear,
    // nonuniform scaling and singular matrices: they cannot preserve circular arcs.
    [[nodiscard]] double signedSimilarityScale() const;

  private:
    double a_ = 1, b_ = 0, tx_ = 0, c_ = 0, d_ = 1, ty_ = 0;
};
} // namespace pd
