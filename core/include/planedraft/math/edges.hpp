// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/entities/primitives.hpp>

#include <functional>
#include <optional>
#include <variant>

namespace pd {
struct Segment {
    Vec2 a, b;
};
struct Ring {
    Vec2 center;
    double radius;
};
using Stroke = std::variant<Segment, Ring, CircularArc>;
using StrokeSink = std::function<void(const Stroke&)>;
// Emits the finite, directed model edges in path order. Text and blocks have no cutting edges.
bool visitEdges(const Entity&, const StrokeSink&);
BoundingBox2D boundsOf(const Stroke& stroke);
Vec2 closestOn(const Stroke& stroke, Vec2 cursor);
// Emits isolated hits (and finite ends of a collinear overlap), clipped to BOTH directed sweeps.
void intersections(const Stroke& lhs, const Stroke& rhs, const std::function<void(Vec2)>& hit);
// Intersections of unbounded supporting lines/circles, for joins and extensions.
Intersection intersectSupports(const Stroke&, const Stroke&);
} // namespace pd
