// SPDX-License-Identifier: LGPL-2.1-or-later
#include "strokes.hpp"

#include <planedraft/drafting.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace pd {
namespace {
std::array<Vec2, 4> corners(const BoundingBox2D& box) {
    return {box.min(), Vec2{box.max().x, box.min().y}, box.max(), Vec2{box.min().x, box.max().y}};
}
} // namespace

void clipFace(const SolidFace& face, const BoundingBox2D& box, const std::function<void(Vec2)>& vertex) {
    auto& v = face.vertices();
    std::vector<Vec2> polygon(v.begin(), v.end()), out;
    for (int axis = 0; axis < 4 && !polygon.empty(); ++axis) {
        bool x = (axis < 2), low = (axis % 2 == 0);
        double edge = x ? (low ? box.min().x : box.max().x) : (low ? box.min().y : box.max().y);
        auto dist = [&](Vec2 p) { return ((x ? p.x : p.y) - edge) * (low ? 1 : -1); };
        out.clear();
        auto a = polygon.back();
        for (auto b : polygon) {
            auto da = dist(a), db = dist(b);
            if ((da >= 0) != (db >= 0))
                out.push_back(a + (b - a) * (da / (da - db)));
            if (db >= 0)
                out.push_back(b);
            a = b;
        }
        polygon.swap(out);
    }
    for (size_t i = 2; i < polygon.size(); ++i) {
        vertex(polygon[0]);
        vertex(polygon[i - 1]);
        vertex(polygon[i]);
    }
}
void visitStrokes(const Entity& e, const StrokeSink& sink, TextStroker text, const Document* doc) {
    if (visitEdges(e, sink))
        return;
    if (auto* label = dynamic_cast<const Text*>(&e)) {
        if (text)
            text(*label, sink);
    } else if (auto f = dynamic_cast<const SolidFace*>(&e)) {
        auto& v = f->vertices();
        for (int i = 0; i < 3; ++i)
            sink(Segment{v[i], v[(i + 1) % 3]});
    } else if (dynamic_cast<const BlockReference*>(&e) || dynamic_cast<const Hatch*>(&e) ||
               dynamic_cast<const Dimension*>(&e))
        visitGraphics(
            doc, e, [&](const Entity& leaf, const StrokeStyle&, LayerId) { visitStrokes(leaf, sink, text); });
}
std::optional<Segment> clipSegment(Segment s, const BoundingBox2D& box) {
    // Parametric slab clipping before narrowing vertices to float. Closed edges count as hits.
    if (box.contains(s.a) && box.contains(s.b)) {
        return s;
    }
    const auto d = s.b - s.a;
    double lo = 0, hi = 1;
    auto slab = [&](double start, double delta, double min, double max) {
        if (delta == 0) {
            return start >= min && start <= max;
        }
        double t = (min - start) / delta, u = (max - start) / delta;
        if (t > u) {
            std::swap(t, u);
        }
        lo = std::max(lo, t);
        hi = std::min(hi, u);
        return lo <= hi;
    };
    if (box.empty() || !slab(s.a.x, d.x, box.min().x, box.max().x) ||
        !slab(s.a.y, d.y, box.min().y, box.max().y)) {
        return std::nullopt;
    }
    auto at = [&](double t) {
        const auto p = t == 0 ? s.a : t == 1 ? s.b : Vec2{std::fma(t, d.x, s.a.x), std::fma(t, d.y, s.a.y)};
        // Re-evaluating a boundary parameter can round one ULP outside the closed slab.
        return Vec2{std::clamp(p.x, box.min().x, box.max().x), std::clamp(p.y, box.min().y, box.max().y)};
    };
    return Segment{at(lo), at(hi)};
}
bool crosses(const Stroke& c, const BoundingBox2D& box) {
    if (auto* s = std::get_if<Segment>(&c)) {
        return clipSegment(*s, box).has_value();
    }
    auto bound = boundsOf(c);
    if (!box.intersects(bound)) {
        return false;
    }
    if (box.contains(bound)) {
        return true;
    }
    bool hit = false;
    const auto pts = corners(box);
    for (std::size_t i = 0; i < 4 && !hit; ++i) {
        intersections(c, Segment{pts[i], pts[(i + 1) % 4]}, [&](Vec2) { hit = true; });
    }
    return hit;
}
} // namespace pd
