// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>
#include <planedraft/edit.hpp>
#include <planedraft/math/edges.hpp>
#include <stdexcept>
namespace pd::edit::detail {
inline constexpr double tau = 2 * std::numbers::pi;
inline void require(bool ok, const char* message) {
    if (!ok)
        throw std::invalid_argument(message);
}
inline const Entity& entity(const Document& d, EntityId id) {
    auto e = d.entity(id);
    require(e, "Unknown entity.");
    return *e;
}
inline std::unique_ptr<Entity> styled(const Entity& src, std::unique_ptr<Entity> dst) {
    dst->traits = src.traits;
    return dst;
}
inline auto edges(const Entity& e) {
    std::vector<Stroke> out;
    require(visitEdges(e, [&](const Stroke& s) { out.push_back(s); }),
            "Expected a line, circle, arc or polyline.");
    return out;
}
inline double angle(Vec2 v) { return std::atan2(v.y, v.x); }
inline double directed(Vec2 start, Vec2 end, double sign) {
    double a = std::atan2(start.cross(end), start.dot(end)) * (sign < 0 ? -1 : 1);
    return a < 0 ? a + tau : a;
}
inline Vec2 heading(double a) { return {std::cos(a), std::sin(a)}; }
inline Vec2 at(const Stroke& s, double f) {
    if (auto l = std::get_if<Segment>(&s))
        return {std::lerp(l->a.x, l->b.x, f), std::lerp(l->a.y, l->b.y, f)};
    if (auto r = std::get_if<Ring>(&s))
        return r->center + heading(f * tau) * r->radius;
    return std::get<CircularArc>(s).pointAt(f);
}
inline size_t nearest(const std::vector<Stroke>& path, Vec2 p) {
    require(p.finite(), "Invalid pick point.");
    double best = std::numeric_limits<double>::infinity();
    size_t index = 0;
    for (size_t i = 0; i < path.size(); ++i) {
        double dist = (closestOn(path[i], p) - p).length();
        if (dist < best) {
            best = dist;
            index = i;
        }
    }
    return index;
}
} // namespace pd::edit::detail
