// SPDX-License-Identifier: LGPL-2.1-or-later
#include "detail.hpp"
namespace pd::edit {
using namespace detail;
namespace {
double leftSide(const Stroke& s, Vec2 side) {
    if (auto l = std::get_if<Segment>(&s))
        return (l->b - l->a).normalized().cross(side - l->a);
    if (auto a = std::get_if<CircularArc>(&s))
        return (a->radius() - (side - a->center()).length()) * (a->sweep() > 0 ? 1 : -1);
    auto r = std::get<Ring>(s);
    return r.radius - (side - r.center).length();
}
Stroke parallel(const Stroke& s, double left) {
    if (auto l = std::get_if<Segment>(&s)) {
        auto n = (l->b - l->a).normalized().leftNormal() * left;
        return Segment{l->a + n, l->b + n};
    }
    if (auto a = std::get_if<CircularArc>(&s))
        return CircularArc(a->center(), a->radius() - (a->sweep() > 0 ? left : -left), a->startAngle(),
                           a->sweep());
    auto r = std::get<Ring>(s);
    r.radius -= left;
    require(r.radius > EPSILON, "Offset collapses the circle.");
    return r;
}
struct ParallelEdge {
    Stroke curve;
    Vec2 start, end;
    double joinBulge = 0; // optional circular join from end to next.start
};
Vec2 tangentAt(const Stroke& s, double t) {
    if (auto l = std::get_if<Segment>(&s))
        return (l->b - l->a).normalized();
    auto& a = std::get<CircularArc>(s);
    return ((a.pointAt(t) - a.center()) / a.radius()).leftNormal() * (a.sweep() > 0 ? 1 : -1);
}
// One simple offset path is supported, not polygon clipping into islands. Detect crossings on the
// analytic result with a private scratch index (never the document's index).
void requireSimple(const Polyline& poly) {
    auto path = edges(poly);
    auto tree = makeRTree();
    for (size_t i = 0; i < path.size(); ++i) {
        auto box = boundsOf(path[i]);
        for (auto j : tree->query(box)) {
            intersections(path[j], path[i], [&](Vec2 p) {
                if (j + 1 == i && (p - at(path[i], 0)).length() <= EPSILON)
                    return;
                if (poly.closed() && j == 0 && i + 1 == path.size() &&
                    (p - at(path[0], 0)).length() <= EPSILON)
                    return;
                throw std::invalid_argument("Offset would self-intersect.");
            });
        }
        tree->insert(i, box);
    }
}
std::unique_ptr<Polyline> parallelPath(const Polyline& p, const std::vector<Stroke>& path, double left) {
    std::vector<ParallelEdge> out;
    for (const auto& s : path) {
        auto offset = parallel(s, left);
        out.push_back({offset, at(offset, 0), at(offset, 1)});
    }
    // Miter at the closest intersection of the adjacent analytic supports, including the closing
    // join. Tangent joins already meet. Never bridge disjoint supports with a fictitious chord.
    for (size_t j = 0; j < (p.closed() ? out.size() : out.size() - 1); ++j) {
        auto& a = out[j];
        auto& b = out[(j + 1) % out.size()];
        Vec2 join;
        if ((a.end - b.start).length() <= EPSILON)
            join = midpoint(a.end, b.start);
        else {
            auto hit = intersectSupports(a.curve, b.curve);
            using K = Intersection::Kind;
            if (hit.kind != K::OnePoint && hit.kind != K::TwoPoints) {
                auto u = tangentAt(path[j], 1), v = tangentAt(path[(j + 1) % path.size()], 0);
                double turn = std::atan2(u.cross(v), u.dot(v));
                require(turn * left < 0 && std::abs(turn) < std::numbers::pi - EPSILON,
                        "Offset edges have no unique join.");
                a.joinBulge = std::tan(turn / 4);
                continue; // outside corner, exact radius |left| about the source vertex
            }
            auto corner = at(path[j], 1);
            join = hit.first;
            if (hit.kind == K::TwoPoints && (hit.second - corner).length() < (join - corner).length())
                join = hit.second;
        }
        a.end = b.start = join;
    }
    std::vector<Vertex> v;
    for (auto& e : out) {
        double bulge = 0;
        if (auto l = std::get_if<Segment>(&e.curve))
            require((e.end - e.start).dot((l->b - l->a).normalized()) > EPSILON,
                    "Offset reverses or collapses an edge.");
        else {
            const auto& arc = std::get<CircularArc>(e.curve);
            double sgn = arc.sweep() > 0 ? 1 : -1;
            // Signed endpoint adjustments on the nearest branch; reject a collapsed edge rather
            // than turning it into an almost complete circle by wrapping the angle.
            auto diff = [&](Vec2 a, Vec2 b) {
                auto u = a - arc.center(), v = b - arc.center();
                return std::atan2(u.cross(v), u.dot(v));
            };
            double sweep = arc.sweep() + diff(arc.pointAt(1), e.end) - diff(arc.pointAt(0), e.start);
            require(sgn * sweep * arc.radius() > EPSILON && std::abs(sweep) < tau,
                    "Offset collapses or wraps an arc.");
            bulge = std::tan(sweep / 4);
        }
        v.push_back({e.start, bulge});
        if (e.joinBulge)
            v.push_back({e.end, e.joinBulge});
    }
    if (!p.closed())
        v.push_back({out.back().end, 0});
    auto result = std::make_unique<Polyline>(std::move(v), p.closed());
    requireSimple(*result);
    return result;
}
struct KeptRay {
    Vec2 outer, unit;
    bool keepStart;
};
KeptRay keep(const Line& l, Vec2 corner, Vec2 click) {
    auto u = (l.end() - l.start()).normalized();
    double t = (click - corner).dot(u);
    if (std::abs(t) <= EPSILON)
        t = (midpoint(l.start(), l.end()) - corner).dot(u);
    require(std::abs(t) > EPSILON, "Pick the part of the line to keep.");
    bool start = t < 0;
    auto outer = start ? l.start() : l.end();
    auto ray = start ? -u : u;
    require((outer - corner).dot(ray) > EPSILON, "No segment on the selected side.");
    return {outer, ray, start};
}
} // namespace
Plan offset(const Document& d, EntityId id, double distance, Vec2 side) {
    require(std::isfinite(distance) && distance > 0, "Offset distance must be positive.");
    auto& e = entity(d, id);
    auto path = edges(e);
    double which = leftSide(path[nearest(path, side)], side);
    require(std::abs(which) > EPSILON, "Pick a side away from the curve.");
    double left = std::copysign(distance, which);
    std::unique_ptr<Entity> result;
    if (auto p = dynamic_cast<const Polyline*>(&e))
        result = parallelPath(*p, path, left);
    else {
        auto s = parallel(path.front(), left);
        if (auto l = std::get_if<Segment>(&s))
            result = std::make_unique<Line>(l->a, l->b);
        else if (auto r = std::get_if<Ring>(&s))
            result = std::make_unique<Circle>(r->center, r->radius);
        else
            result = std::make_unique<Arc>(std::get<CircularArc>(s));
    }
    Plan plan(d);
    plan.put(0, styled(e, std::move(result)));
    return plan;
}
Plan fillet(const Document& d, EntityId a, Vec2 pickA, EntityId b, Vec2 pickB, double r) {
    require(a != b && r >= 0 && std::isfinite(r) && pickA.finite() && pickB.finite(),
            "Invalid fillet parameters.");
    auto la = dynamic_cast<const Line*>(&entity(d, a)), lb = dynamic_cast<const Line*>(&entity(d, b));
    require(la && lb, "Fillet requires two lines.");
    auto hit = intersectSupports(Segment{la->start(), la->end()}, Segment{lb->start(), lb->end()});
    require(hit.kind == Intersection::Kind::OnePoint, "Parallel lines cannot form this fillet.");
    auto corner = hit.first;
    auto x = keep(*la, corner, pickA), y = keep(*lb, corner, pickB);
    double cross = x.unit.cross(y.unit), theta = std::atan2(std::abs(cross), x.unit.dot(y.unit));
    double trim = r / std::tan(theta / 2);
    Plan plan(d);
    auto tangent = [&](const Line& l, const KeptRay& k) {
        require((k.outer - corner).length() - trim > EPSILON, "Fillet radius is too large.");
        auto p = corner + k.unit * trim;
        plan.put(l.id(),
                 styled(l, std::make_unique<Line>(k.keepStart ? k.outer : p, k.keepStart ? p : k.outer)));
        return p;
    };
    auto first = tangent(*la, x);
    tangent(*lb, y);
    if (r > 0) {
        auto center = first + x.unit.leftNormal() * std::copysign(r, cross);
        auto sweep = -std::copysign(std::numbers::pi - theta, cross);
        plan.put(0, styled(*la, std::make_unique<Arc>(CircularArc(center, r, angle(first - center), sweep))));
    }
    return plan;
}
} // namespace pd::edit
