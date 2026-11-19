// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/math/geometry.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace pd {
bool Vec2::finite() const { return std::isfinite(x) && std::isfinite(y); }
double Vec2::length() const { return std::hypot(x, y); }
Vec2 Vec2::normalized() const {
    const double n = length();
    if (!std::isfinite(n) || n <= EPSILON) {
        throw std::invalid_argument("cannot normalize this vector");
    }
    return *this / n;
}

Vec2 midpoint(Vec2 a, Vec2 b) { return {std::midpoint(a.x, b.x), std::midpoint(a.y, b.y)}; }

BoundingBox2D::BoundingBox2D(Vec2 min, Vec2 max) : min_(min), max_(max) {
    if (!min.finite() || !max.finite() || min.x > max.x || min.y > max.y) {
        throw std::invalid_argument("invalid AABB");
    }
}

BoundingBox2D BoundingBox2D::fromPoints(Vec2 a, Vec2 b) {
    BoundingBox2D result;
    result.include(a);
    result.include(b);
    return result;
}

bool BoundingBox2D::contains(Vec2 p) const {
    return min_.x <= p.x && p.x <= max_.x && min_.y <= p.y && p.y <= max_.y;
}

bool BoundingBox2D::contains(const BoundingBox2D& other) const {
    return !other.empty() && contains(other.min_) && contains(other.max_);
}

bool BoundingBox2D::intersects(const BoundingBox2D& other) const {
    return !empty() && !other.empty() && min_.x <= other.max_.x && max_.x >= other.min_.x &&
           min_.y <= other.max_.y && max_.y >= other.min_.y;
}

void BoundingBox2D::include(Vec2 p) {
    if (!p.finite()) {
        throw std::invalid_argument("non-finite point in AABB");
    }
    min_ = {std::min(min_.x, p.x), std::min(min_.y, p.y)};
    max_ = {std::max(max_.x, p.x), std::max(max_.y, p.y)};
}

void BoundingBox2D::include(const BoundingBox2D& other) {
    if (!other.empty()) {
        include(other.min_);
        include(other.max_);
    }
}

Matrix3x3::Matrix3x3(double a, double b, double tx, double c, double d, double ty)
    : a_(a), b_(b), tx_(tx), c_(c), d_(d), ty_(ty) {
    for (double value : {a, b, tx, c, d, ty}) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("non-finite affine matrix");
        }
    }
}

Matrix3x3 Matrix3x3::translation(Vec2 v) { return {1, 0, v.x, 0, 1, v.y}; }
Matrix3x3 Matrix3x3::rotation(double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return {c, -s, 0, s, c, 0};
}

Matrix3x3 Matrix3x3::scaling(double sx, double sy) { return {sx, 0, 0, 0, sy, 0}; }
Matrix3x3 Matrix3x3::reflection(Vec2 p, Vec2 axisDirection) {
    const auto u = axisDirection.normalized();
    Matrix3x3 mirror{2 * u.x * u.x - 1, 2 * u.x * u.y, 0, 2 * u.x * u.y, 2 * u.y * u.y - 1, 0};
    return translation(p) * mirror * translation(p * -1);
}

Vec2 Matrix3x3::applyVector(Vec2 v) const { return {a_ * v.x + b_ * v.y, c_ * v.x + d_ * v.y}; }
Vec2 Matrix3x3::applyPoint(Vec2 p) const { return applyVector(p) + Vec2{tx_, ty_}; }
Matrix3x3 Matrix3x3::operator*(const Matrix3x3& r) const {
    auto x = applyVector({r.a_, r.c_}), y = applyVector({r.b_, r.d_}), t = applyPoint({r.tx_, r.ty_});
    return {x.x, y.x, t.x, x.y, y.y, t.y};
}

BoundingBox2D Matrix3x3::applyBounds(const BoundingBox2D& box) const {
    BoundingBox2D out;
    if (!box.empty()) {
        out.include(applyPoint(box.min()));
        out.include(applyPoint(box.max()));
        out.include(applyPoint({box.min().x, box.max().y}));
        out.include(applyPoint({box.max().x, box.min().y}));
    }
    return out;
}

bool Matrix3x3::invertible() const {
    Vec2 x{a_, c_}, y{b_, d_};
    const auto sx = x.length(), sy = y.length();
    return sx > 0 && sy > 0 && std::isfinite(sx) && std::isfinite(sy) &&
           std::abs((x / sx).cross(y / sy)) > EPSILON;
}

double Matrix3x3::signedSimilarityScale() const {
    Vec2 x{a_, c_}, y{b_, d_};
    const double sx = x.length(), sy = y.length();
    if (sx == 0 || sy == 0 || !std::isfinite(sx) || !std::isfinite(sy) ||
        std::abs(sx - sy) > EPSILON * std::max(sx, sy) || std::abs((x / sx).dot(y / sy)) > EPSILON) {
        throw NotSimilarity{};
    }
    return std::copysign(sx, (x / sx).cross(y / sy));
}
} // namespace pd
