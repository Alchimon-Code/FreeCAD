// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/math/curves.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace pd {
namespace {
constexpr double tau = 2 * std::numbers::pi;
void checkCircle(Vec2 c, double r) {
    if (!c.finite() || !std::isfinite(r) || r <= EPSILON || !(c + Vec2{r, r}).finite() ||
        !(c - Vec2{r, r}).finite()) {
        throw std::invalid_argument("invalid circle");
    }
}

Vec2 direction(double angle) { return {std::cos(angle), std::sin(angle)}; }
} // namespace

Vec2 closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b) {
    if (!p.finite() || !a.finite() || !b.finite()) {
        throw std::invalid_argument("non-finite projection input");
    }
    const auto ab = b - a, ap = p - a;
    const auto len = ab.length();
    if (!std::isfinite(len) || !ap.finite()) {
        throw std::overflow_error("unrepresentable projection");
    }
    if (len <= EPSILON) {
        return a;
    }
    const Vec2 u = ab / len;
    const double t = ap.dot(u);
    if (t <= 0) {
        return a;
    }
    if (t >= len) {
        return b;
    }
    return a + u * t;
}

Vec2 closestPointOnCircle(Vec2 p, Vec2 center, double r) {
    checkCircle(center, r);
    if (!p.finite()) {
        throw std::invalid_argument("non-finite projection input");
    }
    auto v = p - center;
    double len = v.length();
    if (!std::isfinite(len)) {
        throw std::overflow_error("unrepresentable projection");
    }
    return center + (len == 0 ? Vec2{1, 0} : v / len) * r;
}

Intersection intersectSegments(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    if (!a.finite() || !b.finite() || !c.finite() || !d.finite()) {
        throw std::invalid_argument("non-finite segment");
    }
    using K = Intersection::Kind;
    auto u = b - a, v = d - c;
    const double lu = u.length(), lv = v.length();
    if (!std::isfinite(lu) || !std::isfinite(lv) || !(c - a).finite() || !(d - a).finite()) {
        throw std::overflow_error("unrepresentable segment intersection");
    }
    if (lu <= EPSILON) {
        return (a - closestPointOnSegment(a, c, d)).length() <= EPSILON ? Intersection{K::OnePoint, a}
                                                                        : Intersection{};
    }
    if (lv <= EPSILON) {
        return (c - closestPointOnSegment(c, a, b)).length() <= EPSILON ? Intersection{K::OnePoint, c}
                                                                        : Intersection{};
    }
    u = u / lu;
    v = v / lv;
    const auto ac = c - a;
    if (std::abs(u.cross(ac)) <= EPSILON && std::abs(u.cross(d - a)) <= EPSILON) {
        const auto t1 = u.dot(ac), t2 = u.dot(d - a);
        const auto lo = std::max(0.0, std::min(t1, t2)), hi = std::min(lu, std::max(t1, t2));
        if (hi + EPSILON < lo) {
            return {};
        }
        if (hi - lo <= EPSILON) {
            return {K::OnePoint, a + u * std::clamp(lo, 0.0, lu)};
        }
        return {K::Overlap, a + u * lo, a + u * hi};
    }
    const auto den = u.cross(v);
    if (den == 0) {
        return {};
    }
    const auto t = ac.cross(v) / den, s = ac.cross(u) / den;
    if (t < -EPSILON || t > lu + EPSILON || s < -EPSILON || s > lv + EPSILON) {
        return {};
    }
    return {K::OnePoint, a + u * std::clamp(t, 0.0, lu)};
}

Intersection intersectSegmentCircle(Vec2 a, Vec2 b, Vec2 center, double radius) {
    checkCircle(center, radius);
    if (!a.finite() || !b.finite()) {
        throw std::invalid_argument("non-finite segment");
    }
    const auto v = b - a, rel = center - a;
    const auto length = v.length();
    if (!std::isfinite(length) || !rel.finite()) {
        throw std::overflow_error("unrepresentable segment-circle intersection");
    }
    using K = Intersection::Kind;
    if (length <= EPSILON) {
        return std::abs(rel.length() - radius) <= EPSILON ? Intersection{K::OnePoint, a} : Intersection{};
    }
    const Vec2 u = v / length;
    const double dist = std::abs(u.cross(rel)), middle = u.dot(rel);
    if (dist - radius > EPSILON) {
        return {};
    }
    Intersection result;
    auto include = [&](double t) {
        if (t < -EPSILON || t > length + EPSILON) {
            return;
        }
        const auto p = t <= 0 ? a : t >= length ? b : a + u * t;
        if (result.kind == K::None) {
            result = {K::OnePoint, p};
        } else if ((p - result.first).length() > EPSILON) {
            result.kind = K::TwoPoints;
            result.second = p;
        }
    };
    if (std::abs(dist - radius) <= EPSILON) {
        include(middle);
    } else {
        const double h = std::sqrt((radius - dist) / radius * (1 + dist / radius)) * radius;
        include(middle - h);
        include(middle + h);
    }
    return result;
}

Intersection intersectCircles(Vec2 c1, double r1, Vec2 c2, double r2) {
    checkCircle(c1, r1);
    checkCircle(c2, r2);
    using K = Intersection::Kind;
    const auto delta = c2 - c1;
    const auto dist = delta.length();
    if (dist <= EPSILON && std::abs(r1 - r2) <= EPSILON) {
        return {K::Coincident};
    }
    if (!std::isfinite(dist)) {
        throw std::overflow_error("unrepresentable centre distance");
    }
    if (dist == 0) {
        return {};
    }
    // Classify contact in model units before normalizing; scaling a 1:2 tangent pair
    // to thirds can introduce a spurious gap larger than the scaled tolerance.
    const double sum = r1 + r2, difference = std::abs(r1 - r2);
    if (dist - sum > EPSILON || difference - dist > EPSILON) {
        return {};
    }
    const auto u = delta / dist;
    if (std::abs(dist - sum) <= EPSILON) {
        return {K::OnePoint, c1 + u * r1};
    }
    if (std::abs(dist - difference) <= EPSILON) {
        return {K::OnePoint, c1 + u * (r1 >= r2 ? r1 : -r1)};
    }
    // Normalize before squaring: radii can exceed sqrt(DBL_MAX).
    const double scale = std::max({r1, r2, dist});
    const double a = r1 / scale, b = r2 / scale, d = dist / scale;
    const double x = 0.5 * (d + ((r1 - r2) / dist) * (a + b));
    const auto foot = c1 + u * (x * scale);
    const auto offset = u.leftNormal() * (scale * std::sqrt(std::max(0.0, (a - x) * (a + x))));
    return {K::TwoPoints, foot + offset, foot - offset};
}

CircularArc::CircularArc(Vec2 c, double r, double start, double sweep)
    : center_(c), radius_(r), start_(std::remainder(start, tau)), sweep_(sweep) {
    checkCircle(c, r);
    if (!std::isfinite(start_) || !std::isfinite(sweep) || sweep == 0 || std::abs(sweep) >= tau) {
        throw std::invalid_argument("arc requires a finite nonzero sweep smaller than one turn");
    }
    first_ = center_ + direction(start_) * radius_;
    last_ = center_ + direction(start_ + sweep_) * radius_;
}

CircularArc CircularArc::fromBulge(Vec2 a, Vec2 b, double bulge) {
    const auto chord = b - a;
    const auto n = chord.normalized().leftNormal();
    if (bulge == 0 || !std::isfinite(bulge)) {
        throw std::invalid_argument("expected nonzero finite bulge");
    }
    const auto quarter = chord.length() * 0.25;
    const auto c = midpoint(a, b) + n * (quarter / bulge - quarter * bulge);
    const auto r = quarter / std::abs(bulge) + quarter * std::abs(bulge);
    CircularArc result(c, r, std::atan2(a.y - c.y, a.x - c.x), 4 * std::atan(bulge));
    result.first_ = a;
    result.last_ = b; // exact chord endpoints, including very shallow arcs
    return result;
}

double CircularArc::along(Vec2 radialDirection) const {
    auto start = (first_ - center_) / radius_;
    double a = std::atan2(start.cross(radialDirection), start.dot(radialDirection)) * (sweep_ > 0 ? 1 : -1);
    return a < 0 ? a + tau : a;
}

std::optional<double> CircularArc::fractionAt(Vec2 radialDirection) const {
    const double a = along(radialDirection);
    const double tolerance =
        std::max(8 * std::numeric_limits<double>::epsilon(), std::min(EPSILON, EPSILON / radius_));
    if (a <= std::abs(sweep_) + tolerance) {
        return std::min(a / std::abs(sweep_), 1.0);
    }
    if (tau - a <= tolerance) {
        return 0;
    }
    return std::nullopt;
}

bool CircularArc::containsAngle(double angle) const { return fractionAt(direction(angle)).has_value(); }
std::optional<Vec2> CircularArc::quadrantPoint(unsigned index) const {
    constexpr std::array<Vec2, 4> axes{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    const auto axis = axes.at(index);
    const auto t = fractionAt(axis);
    if (!t) {
        return std::nullopt;
    }
    return std::abs(sweep_) < 4 * std::sqrt(EPSILON) ? pointAt(*t) : center_ + axis * radius_;
}

Vec2 CircularArc::pointAt(double fraction) const {
    if (!(fraction >= 0 && fraction <= 1)) {
        throw std::invalid_argument("arc fraction outside [0,1]");
    }
    if (fraction == 0) {
        return first_;
    }
    if (fraction == 1) {
        return last_;
    }
    // Rotate from the nearer endpoint using cos(t)-1 = -2*sin(t/2)^2. This preserves
    // a small sagitta without summing an almost cancelling centre and radius.
    bool fromStart = fraction <= 0.5;
    Vec2 anchor = fromStart ? first_ : last_, radial = anchor - center_;
    double t = (fromStart ? fraction : fraction - 1) * sweep_, s = std::sin(t / 2);
    double cosm1 = -2 * s * s, sint = std::sin(t);
    return {std::fma(-radial.y, sint, std::fma(radial.x, cosm1, anchor.x)),
            std::fma(radial.x, sint, std::fma(radial.y, cosm1, anchor.y))};
}

Vec2 CircularArc::closestPoint(Vec2 p) const {
    if (!p.finite()) {
        throw std::invalid_argument("non-finite projection input");
    }
    const auto v = p - center_;
    const auto len = v.length();
    if (!std::isfinite(len)) {
        throw std::overflow_error("unrepresentable projection");
    }
    if (len > 0) {
        double a = along(v / len);
        if (a <= std::abs(sweep_)) {
            return pointAt(a / std::abs(sweep_));
        }
    }
    return (p - first_).length() <= (p - last_).length() ? first_ : last_;
}

BoundingBox2D CircularArc::bounds() const {
    auto box = BoundingBox2D::fromPoints(first_, last_);
    for (unsigned i = 0; i < 4; ++i) {
        if (auto p = quadrantPoint(i)) {
            box.include(*p);
        }
    }
    return box;
}

CircularArc CircularArc::transformed(const Matrix3x3& mat) const {
    auto scale = mat.signedSimilarityScale();
    auto start = mat.applyVector(direction(start_));
    CircularArc result(mat.applyPoint(center_), radius_ * std::abs(scale), std::atan2(start.y, start.x),
                       scale < 0 ? -sweep_ : sweep_);
    result.first_ = mat.applyPoint(first_);
    result.last_ = mat.applyPoint(last_);
    if (!result.first_.finite() || !result.last_.finite()) {
        throw std::invalid_argument("non-finite arc endpoint");
    }
    return result;
}
} // namespace pd
