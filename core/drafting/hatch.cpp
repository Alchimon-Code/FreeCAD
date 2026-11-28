// SPDX-License-Identifier: LGPL-2.1-or-later
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <planedraft/entities/annotation.hpp>
namespace pd {
namespace {
constexpr double pi = std::numbers::pi;
constexpr size_t maxPieces = 200000;
void budget(size_t n) {
    if (n > maxPieces)
        throw std::length_error("hatch geometry limit exceeded");
}
// Scan edges in coordinates relative to the pattern origin. Split arcs at Y extrema, then every
// edge is Y-monotone. The half-open rule counts a shared vertex once and a tangency zero/twice.
struct Edge {
    Vec2 a, b;
    Vec2 center{};
    double signedRadius = 0;
    bool active(double y) const { return y >= std::min(a.y, b.y) && y < std::max(a.y, b.y); }
    double x(double y) const {
        if (y == a.y)
            return a.x;
        if (y == b.y)
            return b.x;
        if (signedRadius == 0)
            return std::lerp(a.x, b.x, (y - a.y) / (b.y - a.y));
        double r = std::abs(signedRadius), dy = std::abs(y - center.y);
        return center.x + std::copysign(std::sqrt(std::max(0., (r - dy) * (r + dy))), signedRadius);
    }
};
void arcEdges(std::vector<Edge>& out, Vec2 p, Vec2 q, double bulge) {
    auto a = CircularArc::fromBulge(p, q, bulge);
    std::vector<double> t{0, 1};
    for (double ang : {pi / 2, -pi / 2}) {
        double turn = std::fmod((ang - a.startAngle()) * std::copysign(1., a.sweep()), 2 * pi);
        if (turn < 0)
            turn += 2 * pi;
        double f = turn / std::abs(a.sweep());
        if (f > 1e-14 && f < 1 - 1e-14)
            t.push_back(f);
    }
    std::sort(t.begin(), t.end());
    for (size_t i = 1; i < t.size(); ++i) {
        auto mid = a.pointAt((t[i - 1] + t[i]) * .5);
        out.push_back({i == 1 ? p : a.pointAt(t[i - 1]), i + 1 == t.size() ? q : a.pointAt(t[i]), a.center(),
                       std::copysign(a.radius(), mid.x - a.center().x)});
    }
}
} // namespace
std::vector<Vec2> polygonize(const Polyline& p, double tol) {
    if (!std::isfinite(tol) || tol <= 0)
        throw std::invalid_argument("invalid chord tolerance");
    std::vector<Vec2> pts;
    auto v = p.vertices();
    for (size_t i = 0; i < v.size(); ++i) {
        pts.push_back(v[i].point);
        if (!v[i].bulge || (!p.closed() && i + 1 == v.size()))
            continue;
        auto a = CircularArc::fromBulge(v[i].point, v[(i + 1) % v.size()].point, v[i].bulge);
        double step = 4 * std::asin(std::sqrt(std::min(.5, tol / (2 * a.radius()))));
        double n = std::ceil(std::abs(a.sweep()) / std::min(step, pi / 2));
        if (!std::isfinite(n) || n > maxPieces)
            throw std::length_error("curve segmentation limit");
        for (int j = 1; j < n; ++j)
            pts.push_back(a.pointAt(j / n));
        budget(pts.size());
    }
    return pts;
}
Polyline Hatch::boundary(const Entity& e) {
    if (auto p = dynamic_cast<const Polyline*>(&e); p && p->closed())
        return *p;
    if (auto c = dynamic_cast<const Circle*>(&e)) {
        Vec2 r{c->radius(), 0};
        return Polyline({{c->center() + r, 1}, {c->center() - r, 1}}, true);
    }
    throw std::invalid_argument("expected closed polyline or circle");
}
std::vector<PatternFamily> Hatch::families() const {
    double d = 2.5 * scale_;
    switch (pattern_) {
    case HatchPattern::ANSI31:
        return {{angle_ + pi / 4, d}};
    case HatchPattern::Grid:
        return {{angle_, d}, {angle_ + pi / 2, d}};
    case HatchPattern::Concrete:
        return {{angle_, d * 2, d * .4, d * 1.6},
                {angle_ + pi / 3, d * 2, d * .4, d * 1.6},
                {angle_ - pi / 3, d * 2, d * .4, d * 1.6}};
    default:
        return {};
    }
}
Hatch::Hatch(std::vector<Polyline> loops, HatchPattern pat, double scale, double angle)
    : loops_(std::move(loops)), pattern_(pat), scale_(scale), angle_(angle) {
    if (pat < HatchPattern::Solid || pat > HatchPattern::Concrete || loops_.empty() ||
        !std::isfinite(scale) || scale <= EPSILON || !std::isfinite(angle))
        throw std::invalid_argument("invalid hatch parameters");
    size_t vertices = 0;
    for (auto& p : loops_) {
        if (!p.closed())
            throw std::invalid_argument("open hatch boundary");
        vertices += p.vertices().size();
        budget(vertices);
        bounds_.include(p.getBounds());
    }
    auto origin = loops_[0].vertices()[0].point;
    size_t work = 0;
    auto scan = [&](auto& edges, double y) {
        work += edges.size();
        if (work > 20000000)
            throw std::length_error("hatch scan limit");
        std::vector<const Edge*> hits;
        for (auto& e : edges)
            if (e.active(y))
                hits.push_back(&e);
        std::sort(hits.begin(), hits.end(), [&](auto a, auto b) { return a->x(y) < b->x(y); });
        if (hits.size() % 2)
            throw std::invalid_argument("unclosed hatch crossing");
        return hits;
    };
    if (pat == HatchPattern::Solid) {
        std::vector<Edge> edges;
        std::vector<double> levels;
        for (auto& p : loops_) {
            auto pts = polygonize(p, .01);
            for (size_t i = 0; i < pts.size(); ++i) {
                auto a = pts[i] - origin, b = pts[(i + 1) % pts.size()] - origin;
                edges.push_back({a, b});
                levels.push_back(a.y);
            }
            budget(edges.size());
        }
        std::sort(levels.begin(), levels.end());
        levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
        for (size_t i = 1; i < levels.size(); ++i) {
            double a = levels[i - 1], b = levels[i];
            auto hits = scan(edges, std::midpoint(a, b));
            for (size_t j = 0; j < hits.size(); j += 2) {
                Vec2 lo{hits[j]->x(a), a}, hi{hits[j + 1]->x(a), a}, top{hits[j + 1]->x(b), b},
                    left{hits[j]->x(b), b};
                for (auto t : {std::array{lo, hi, top}, std::array{lo, top, left}})
                    if (std::abs((t[1] - t[0]).cross(t[2] - t[0])) > 0) {
                        budget(parts_.size() + 1);
                        parts_.push_back(
                            std::make_shared<SolidFace>(origin + t[0], origin + t[1], origin + t[2]));
                    }
            }
        }
    } else
        for (auto family : families()) {
            if (!std::isfinite(family.spacing))
                throw std::length_error("hatch spacing exceeds range");
            Vec2 u{std::cos(family.angle), std::sin(family.angle)}, n = u.leftNormal();
            auto local = [&](Vec2 p) {
                p = p - origin;
                return Vec2{p.dot(u), p.dot(n)};
            };
            std::vector<Edge> edges;
            for (auto& p : loops_) {
                auto v = p.vertices();
                for (size_t i = 0; i < v.size(); ++i) {
                    auto a = local(v[i].point), b = local(v[(i + 1) % v.size()].point);
                    if (v[i].bulge)
                        arcEdges(edges, a, b, v[i].bulge);
                    else
                        edges.push_back({a, b});
                }
            }
            double lo = 0, hi = 0;
            for (auto& e : edges) {
                lo = std::min({lo, e.a.y, e.b.y});
                hi = std::max({hi, e.a.y, e.b.y});
            }
            double first = std::ceil(lo / family.spacing), last = std::floor(hi / family.spacing);
            if (!(last - first < maxPieces))
                throw std::length_error("hatch too dense");
            for (int k = int(first); k <= int(last); ++k) {
                double y = k * family.spacing;
                auto hits = scan(edges, y);
                for (size_t j = 0; j < hits.size(); j += 2) {
                    auto x = hits[j]->x(y), z = hits[j + 1]->x(y);
                    auto segment = [&](double left, double right) {
                        if (right <= left)
                            return;
                        auto a = origin + n * y + u * left, b = origin + n * y + u * right;
                        if ((b - a).length() > EPSILON) {
                            budget(parts_.size() + 1);
                            parts_.push_back(std::make_shared<Line>(a, b));
                        }
                    };
                    if (!family.dash)
                        segment(x, z);
                    else {
                        auto period = family.dash + family.gap;
                        for (double start = std::floor(x / period) * period; start < z;) {
                            segment(std::max(start, x), std::min(z, start + family.dash));
                            if (++work > 20000000 || !(start + period > start))
                                throw std::length_error("hatch dash limit");
                            start += period;
                        }
                    }
                }
            }
        }
}
void Hatch::transform(const Matrix3x3& m) {
    auto z = m.signedSimilarityScale();
    auto loops = loops_;
    for (auto& p : loops)
        p.transform(m);
    double dir = angle_ + (pattern_ == HatchPattern::ANSI31 ? pi / 4 : 0);
    auto u = m.applyVector({std::cos(dir), std::sin(dir)});
    Hatch h(std::move(loops), pattern_, scale_ * std::abs(z),
            std::atan2(u.y, u.x) - (pattern_ == HatchPattern::ANSI31 ? pi / 4 : 0));
    loops_.swap(h.loops_);
    parts_.swap(h.parts_);
    bounds_ = h.bounds_;
    angle_ = h.angle_;
    scale_ = h.scale_;
}
std::vector<Vec2> Hatch::getSnapPoints(SnapType m) const {
    return hasSnap(m, SnapType::Insertion) ? getGripPoints() : std::vector<Vec2>{};
}
} // namespace pd
