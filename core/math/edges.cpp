// SPDX-License-Identifier: LGPL-2.1-or-later
#include <algorithm>
#include <cmath>
#include <planedraft/math/edges.hpp>
namespace pd {
namespace {
Ring supportCircle(const Stroke& c) {
    if (auto* ring = std::get_if<Ring>(&c)) {
        return *ring;
    }
    const auto& arc = std::get<CircularArc>(c);
    return {arc.center(), arc.radius()};
}
bool onSweep(const Stroke& c, Vec2 p) {
    if (auto* arc = std::get_if<CircularArc>(&c)) {
        auto v = p - arc->center();
        return arc->containsAngle(std::atan2(v.y, v.x));
    }
    return true;
}
} // namespace
bool visitEdges(const Entity& e, const StrokeSink& sink) {
    if (auto* line = dynamic_cast<const Line*>(&e)) {
        sink(Segment{line->start(), line->end()});
    } else if (auto* circle = dynamic_cast<const Circle*>(&e)) {
        sink(Ring{circle->center(), circle->radius()});
    } else if (auto* arc = dynamic_cast<const Arc*>(&e)) {
        sink(arc->curve());
    } else if (auto* poly = dynamic_cast<const Polyline*>(&e)) {
        const auto v = poly->vertices();
        const auto edges = poly->closed() ? v.size() : v.size() - 1;
        for (std::size_t i = 0; i < edges; ++i) {
            const Vec2 b = v[(i + 1) % v.size()].point;
            if (v[i].bulge == 0) {
                sink(Segment{v[i].point, b});
            } else {
                sink(CircularArc::fromBulge(v[i].point, b, v[i].bulge));
            }
        }
    } else
        return false;
    return true;
}
BoundingBox2D boundsOf(const Stroke& c) {
    if (auto* s = std::get_if<Segment>(&c)) {
        return BoundingBox2D::fromPoints(s->a, s->b);
    }
    if (auto* r = std::get_if<Ring>(&c)) {
        const Vec2 half{r->radius, r->radius};
        return {r->center - half, r->center + half};
    }
    return std::get<CircularArc>(c).bounds();
}
Vec2 closestOn(const Stroke& c, Vec2 p) {
    if (auto* s = std::get_if<Segment>(&c)) {
        return closestPointOnSegment(p, s->a, s->b);
    }
    if (auto* r = std::get_if<Ring>(&c)) {
        return closestPointOnCircle(p, r->center, r->radius);
    }
    return std::get<CircularArc>(c).closestPoint(p);
}
void intersections(const Stroke& a, const Stroke& b, const std::function<void(Vec2)>& hit) {
    const auto* s = std::get_if<Segment>(&a);
    const auto* t = std::get_if<Segment>(&b);
    Intersection result;
    if (s && t) {
        result = intersectSegments(s->a, s->b, t->a, t->b);
    } else if (s || t) {
        const auto r = supportCircle(s ? b : a);
        const auto* line = s ? s : t;
        result = intersectSegmentCircle(line->a, line->b, r.center, r.radius);
    } else {
        const auto r = supportCircle(a), q = supportCircle(b);
        result = intersectCircles(r.center, r.radius, q.center, q.radius);
    }
    auto check = [&](Vec2 p) {
        if (onSweep(a, p) && onSweep(b, p)) {
            hit(p);
        }
    };
    using K = Intersection::Kind;
    if (result.kind == K::OnePoint || result.kind == K::TwoPoints || result.kind == K::Overlap) {
        check(result.first);
        if (result.kind != K::OnePoint) {
            check(result.second);
        }
    } else if (result.kind == K::Coincident) {
        // Common support circle: only finite arc boundary points, not an arbitrary point on a ring.
        for (const auto* curve : {&a, &b}) {
            if (const auto* arc = std::get_if<CircularArc>(curve)) {
                check(arc->pointAt(0));
                check(arc->pointAt(1));
            }
        }
    }
}

Intersection intersectSupports(const Stroke& a, const Stroke& b) {
    using K = Intersection::Kind;
    auto s = std::get_if<Segment>(&a), t = std::get_if<Segment>(&b);
    if (s && t) {
        auto u = (s->b - s->a).normalized(), v = (t->b - t->a).normalized();
        double den = u.cross(v);
        if (std::abs(den) <= 8 * std::numeric_limits<double>::epsilon())
            return {std::abs(u.cross(t->a - s->a)) <= EPSILON ? K::Coincident : K::None};
        return {K::OnePoint, s->a + u * ((t->a - s->a).cross(v) / den)};
    }
    if (s || t) {
        const auto& l = *(s ? s : t);
        auto r = supportCircle(s ? b : a);
        auto u = (l.b - l.a).normalized();
        double signedDistance = u.cross(r.center - l.a), delta = std::abs(signedDistance);
        if (delta - r.radius > EPSILON)
            return {};
        auto foot = r.center - u.leftNormal() * signedDistance;
        if (std::abs(delta - r.radius) <= EPSILON)
            return {K::OnePoint, foot};
        auto h = u * (std::sqrt((r.radius - delta) / r.radius * (1 + delta / r.radius)) * r.radius);
        return {K::TwoPoints, foot - h, foot + h};
    }
    auto x = supportCircle(a), y = supportCircle(b);
    return intersectCircles(x.center, x.radius, y.center, y.radius);
}
} // namespace pd
