// SPDX-License-Identifier: LGPL-2.1-or-later
#include "detail.hpp"
#include <numbers>

namespace pd::edit {
using namespace detail;
namespace {
std::vector<EntityId> unique(Selection ids) {
    require(!ids.empty(), "Empty selection.");
    std::vector<EntityId> v(ids.begin(), ids.end());
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}
CircularArc through(Vec2 a, Vec2 mid, Vec2 b) {
    auto x = mid - a, y = b - a;
    auto u = x.normalized(), v = y.normalized();
    auto den = 2 * u.cross(v);
    require(std::abs(den) * std::min(x.length(), y.length()) > EPSILON, "Collinear arc points.");
    auto center = a + (u.leftNormal() * y.length() - v.leftNormal() * x.length()) / den;
    auto start = a - center;
    double sign = x.cross(y) > 0 ? 1 : -1;
    return {center, start.length(), angle(start), sign * directed(start, b - center, sign)};
}
std::unique_ptr<Polyline> poly(std::vector<Vec2> points, bool closed, LayerId layer) {
    std::vector<Vertex> vertices;
    vertices.reserve(points.size());
    for (auto p : points) {
        require(p.finite(), "Coordinates must be finite.");
        vertices.push_back({p, 0});
    }
    auto e = std::make_unique<Polyline>(std::move(vertices), closed);
    e->traits.layer = layer;
    return e;
}
} // namespace

Plan rectangle(const Document& d, Vec2 first, Vec2 second, LayerId layer) {
    require(first.finite() && second.finite(), "Coordinates must be finite.");
    require(std::abs(second.x - first.x) > EPSILON && std::abs(second.y - first.y) > EPSILON,
            "Rectangle needs two corners with different X and Y.");
    Plan p(d);
    p.put(0, poly({first, {second.x, first.y}, second, {first.x, second.y}}, true, layer));
    return p;
}

Plan polygon(const Document& d, Vec2 center, Vec2 vertex, int sides, bool circumscribed,
             LayerId layer) {
    require(center.finite() && vertex.finite(), "Coordinates must be finite.");
    require(sides >= 3 && sides <= 1024, "A regular polygon needs 3 to 1024 sides.");
    auto radius = (vertex - center).length();
    require(radius > EPSILON, "Polygon radius must be positive.");
    // Circumscribed: the picked point is the middle of an edge, so the vertex sits further out.
    if (circumscribed)
        radius /= std::cos(std::numbers::pi / sides);
    require(radius > EPSILON, "Polygon radius is too small.");
    auto a0 = angle(vertex - center);
    std::vector<Vec2> points;
    points.reserve(static_cast<std::size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        auto a = a0 + 2 * std::numbers::pi * i / sides;
        points.push_back(center + heading(a) * radius);
    }
    Plan p(d);
    p.put(0, poly(std::move(points), true, layer));
    return p;
}

Plan circle(const Document& d, Vec2 center, double radius, LayerId layer) {
    require(center.finite() && std::isfinite(radius) && radius > EPSILON,
            "Circle radius must be positive.");
    Plan p(d);
    auto e = std::make_unique<Circle>(center, radius);
    e->traits.layer = layer;
    p.put(0, std::move(e));
    return p;
}

Plan arc(const Document& d, Vec2 start, Vec2 onArc, Vec2 end, LayerId layer) {
    require(start.finite() && onArc.finite() && end.finite(), "Coordinates must be finite.");
    Plan p(d);
    auto e = std::make_unique<Arc>(through(start, onArc, end));
    e->traits.layer = layer;
    p.put(0, std::move(e));
    return p;
}

Plan polyline(const Document& d, std::span<const Vec2> points, bool closed, LayerId layer) {
    require(points.size() >= 2 && (!closed || points.size() >= 3),
            "A polyline needs at least two vertices (three when closed).");
    Plan p(d);
    p.put(0, poly(std::vector<Vec2>(points.begin(), points.end()), closed, layer));
    return p;
}

Plan arrayRectangular(const Document& d, Selection ids, int rows, int columns, Vec2 spacing) {
    require(rows >= 1 && columns >= 1 && rows * columns > 1, "Array needs at least two items.");
    require(rows <= 10000 && columns <= 10000, "Array is too large.");
    require(spacing.finite(), "Array spacing must be finite.");
    auto base = unique(ids);
    Plan p(d);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns; ++c) {
            if (!r && !c)
                continue;
            auto offset = Vec2{spacing.x * c, spacing.y * r};
            for (auto id : base) {
                auto e = entity(d, id).clone();
                e->transform(Matrix3x3::translation(offset));
                p.put(0, std::move(e));
            }
        }
    return p;
}

Plan arrayPolar(const Document& d, Selection ids, Vec2 center, int count, double fillAngle) {
    require(center.finite(), "Array centre must be finite.");
    require(count >= 2 && count <= 10000, "A polar array needs 2 to 10000 items.");
    require(std::isfinite(fillAngle) && std::abs(fillAngle) > EPSILON && std::abs(fillAngle) <= 2 * std::numbers::pi + EPSILON,
            "Fill angle must be between -360 and 360 degrees.");
    auto base = unique(ids);
    const bool full = std::abs(std::abs(fillAngle) - 2 * std::numbers::pi) < 1e-9;
    Plan p(d);
    for (int i = 1; i < count; ++i) {
        auto step = full ? fillAngle / count : fillAngle / (count - 1);
        auto m = Matrix3x3::translation(center) * Matrix3x3::rotation(step * i) *
                 Matrix3x3::translation(-center);
        for (auto id : base) {
            auto e = entity(d, id).clone();
            e->transform(m);
            p.put(0, std::move(e));
        }
    }
    return p;
}

Plan matchProperties(const Document& d, EntityId source, Selection targets) {
    auto& from = entity(d, source);
    Plan p(d);
    for (auto id : unique(targets)) {
        require(id != source, "The source object is also a target.");
        auto e = entity(d, id).clone();
        e->traits.layer = from.traits.layer;
        e->traits.color = from.traits.color;
        e->traits.lineWeight = from.traits.lineWeight;
        e->traits.lineType = from.traits.lineType;
        p.put(id, std::move(e));
    }
    return p;
}

Measurement measure(Vec2 a, Vec2 b) {
    require(a.finite() && b.finite(), "Coordinates must be finite.");
    auto v = b - a;
    auto d = v.length();
    require(d > EPSILON, "The two points must be distinct.");
    auto a2 = std::atan2(v.y, v.x);
    if (a2 < 0)
        a2 += 2 * std::numbers::pi;
    return {d, a2};
}

double area(const Entity& e) {
    if (auto c = dynamic_cast<const Circle*>(&e))
        return std::numbers::pi * c->radius() * c->radius();
    auto p = dynamic_cast<const Polyline*>(&e);
    require(p && p->closed(), "Area needs a closed polyline or a circle.");
    auto v = p->vertices();
    require(v.size() >= 3, "Closed polyline has too few vertices.");
    double sum = 0;
    for (std::size_t i = 0; i < v.size(); ++i)
        sum += v[i].point.cross(v[(i + 1) % v.size()].point);
    sum /= 2;
    // A bulge is a circular segment between its two vertices. Positive bulges add area to a
    // counter-clockwise loop, negative bulges subtract it.
    for (std::size_t i = 0; i < v.size(); ++i) {
        double b = v[i].bulge;
        if (std::abs(b) <= EPSILON)
            continue;
        auto a = v[i].point, c = v[(i + 1) % v.size()].point;
        double chord = (c - a).length();
        double theta = 4 * std::atan(std::abs(b));
        require(std::abs(std::sin(theta / 2)) > EPSILON, "Degenerate bulge in area contour.");
        double r = chord / (2 * std::sin(theta / 2));
        double segment = r * r / 2 * (theta - std::sin(theta));
        sum += b > 0 ? segment : -segment;
    }
    return std::abs(sum);
}
} // namespace pd::edit
