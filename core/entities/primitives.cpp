// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/entities/primitives.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace pd {
namespace {
constexpr std::array<Vec2, 4> axes{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
void checkChord(Vec2 a, Vec2 b) {
    const double len = (b - a).length();
    if (!a.finite() || !b.finite() || !std::isfinite(len) || len <= EPSILON) {
        throw std::invalid_argument("invalid segment endpoints");
    }
}

Vec2 bulgeMidpoint(Vec2 a, Vec2 b, double bulge) {
    return midpoint(a, b) - (b - a).leftNormal() * (0.5 * bulge);
}

void arcQuadrants(std::vector<Vec2>& out, const CircularArc& arc) {
    for (unsigned i = 0; i < 4; ++i) {
        if (auto p = arc.quadrantPoint(i)) {
            out.push_back(*p);
        }
    }
}
} // namespace

Line::Line(Vec2 a, Vec2 b) : start_(a), end_(b) { checkChord(a, b); }
BoundingBox2D Line::getBounds() const { return BoundingBox2D::fromPoints(start_, end_); }
void Line::transform(const Matrix3x3& m) {
    auto a = m.applyPoint(start_), b = m.applyPoint(end_);
    checkChord(a, b);
    start_ = a;
    end_ = b;
}

std::unique_ptr<Entity> Line::clone() const { return std::make_unique<Line>(*this); }
std::vector<Vec2> Line::getSnapPoints(SnapType mask) const {
    std::vector<Vec2> pts;
    if (hasSnap(mask, SnapType::Endpoint)) {
        pts = {start_, end_};
    }
    if (hasSnap(mask, SnapType::Midpoint)) {
        pts.push_back(midpoint(start_, end_));
    }
    return pts;
}

std::vector<Vec2> Line::getGripPoints() const {
    return getSnapPoints(SnapType::Endpoint | SnapType::Midpoint);
}

Circle::Circle(Vec2 c, double r) : center_(c), radius_(r) {
    if (!std::isfinite(r) || r <= EPSILON) {
        throw std::invalid_argument("invalid radius");
    }
    (void)getBounds(); // also checks that all extrema are representable
}

BoundingBox2D Circle::getBounds() const {
    Vec2 ext{radius_, radius_};
    return {center_ - ext, center_ + ext};
}

void Circle::transform(const Matrix3x3& m) {
    Circle result(m.applyPoint(center_), radius_ * std::abs(m.signedSimilarityScale()));
    center_ = result.center_;
    radius_ = result.radius_;
}

std::unique_ptr<Entity> Circle::clone() const { return std::make_unique<Circle>(*this); }
std::vector<Vec2> Circle::getSnapPoints(SnapType mask) const {
    std::vector<Vec2> pts;
    if (hasSnap(mask, SnapType::Center)) {
        pts.push_back(center_);
    }
    if (hasSnap(mask, SnapType::Quadrant)) {
        for (auto v : axes) {
            pts.push_back(center_ + v * radius_);
        }
    }
    return pts;
}

std::vector<Vec2> Circle::getGripPoints() const {
    return getSnapPoints(SnapType::Center | SnapType::Quadrant);
}

std::unique_ptr<Entity> Arc::clone() const { return std::make_unique<Arc>(*this); }
std::vector<Vec2> Arc::getSnapPoints(SnapType mask) const {
    std::vector<Vec2> pts;
    if (hasSnap(mask, SnapType::Endpoint)) {
        pts = {curve_.pointAt(0), curve_.pointAt(1)};
    }
    if (hasSnap(mask, SnapType::Midpoint)) {
        pts.push_back(curve_.pointAt(0.5));
    }
    if (hasSnap(mask, SnapType::Center)) {
        pts.push_back(curve_.center());
    }
    if (hasSnap(mask, SnapType::Quadrant)) {
        arcQuadrants(pts, curve_);
    }
    return pts;
}

std::vector<Vec2> Arc::getGripPoints() const {
    return getSnapPoints(SnapType::Endpoint | SnapType::Midpoint | SnapType::Center);
}

Polyline::Polyline(std::vector<Vertex> vertices, bool closed)
    : vertices_(std::move(vertices)), closed_(closed) {
    if (vertices_.size() < 2 || (!closed && vertices_.back().bulge != 0)) {
        throw std::invalid_argument("polyline needs segments; an open end has no bulge");
    }
    const auto n = vertices_.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto [a, bulge] = vertices_[i];
        bounds_.include(a);
        if (!closed && i + 1 == n) {
            break;
        }
        const auto b = vertices_[(i + 1) % n].point;
        checkChord(a, b);
        if (bulge == 0) {
            continue;
        }
        bounds_.include(CircularArc::fromBulge(a, b, bulge).bounds());
        bounds_.include(bulgeMidpoint(a, b, bulge)); // include the actual snap/grip even after rounding
    }
}

void Polyline::transform(const Matrix3x3& m) {
    bool reflected = false;
    if (std::any_of(vertices_.begin(), vertices_.end(), [](auto& v) { return v.bulge != 0; })) {
        reflected = m.signedSimilarityScale() < 0;
    }
    auto vertices = vertices_;
    for (auto& v : vertices) {
        v.point = m.applyPoint(v.point);
        if (reflected) {
            v.bulge = -v.bulge;
        }
    }
    Polyline result(std::move(vertices), closed_);
    vertices_.swap(result.vertices_);
    bounds_ = result.bounds_;
}

std::unique_ptr<Entity> Polyline::clone() const { return std::make_unique<Polyline>(*this); }
std::vector<Vec2> Polyline::getSnapPoints(SnapType mask) const {
    std::vector<Vec2> pts;
    const auto n = vertices_.size();
    for (std::size_t i = 0; i < n; ++i) {
        auto [a, bulge] = vertices_[i];
        if (hasSnap(mask, SnapType::Endpoint)) {
            pts.push_back(a);
        }
        if (!closed_ && i + 1 == n) {
            break;
        }
        auto b = vertices_[(i + 1) % n].point;
        if (hasSnap(mask, SnapType::Midpoint)) {
            pts.push_back(bulgeMidpoint(a, b, bulge));
        }
        if (bulge != 0 && (hasSnap(mask, SnapType::Center) || hasSnap(mask, SnapType::Quadrant))) {
            auto arc = CircularArc::fromBulge(a, b, bulge);
            if (hasSnap(mask, SnapType::Center)) {
                pts.push_back(arc.center());
            }
            if (hasSnap(mask, SnapType::Quadrant)) {
                arcQuadrants(pts, arc);
            }
        }
    }
    return pts;
}

std::vector<Vec2> Polyline::getGripPoints() const {
    return getSnapPoints(SnapType::Endpoint | SnapType::Midpoint);
}

Text::Text(std::string text, BoundingBox2D ink, Matrix3x3 placement)
    : text_(std::move(text)), ink_(ink), placement_(placement) {
    if (text_.empty() || ink.empty() || !placement.invertible()) {
        throw std::invalid_argument("text requires content and metrics");
    }
    (void)getBounds();
}

void Text::transform(const Matrix3x3& m) {
    auto placement = m * placement_;
    if (!placement.invertible()) {
        throw std::invalid_argument("singular text placement");
    }
    (void)placement.applyBounds(ink_);
    placement_ = placement;
}

std::unique_ptr<Entity> Text::clone() const { return std::make_unique<Text>(*this); }
std::vector<Vec2> Text::getSnapPoints(SnapType mask) const {
    return hasSnap(mask, SnapType::Insertion) ? getGripPoints() : std::vector<Vec2>{};
}

std::vector<Vec2> Text::getGripPoints() const { return {placement_.applyPoint({})}; }
} // namespace pd
