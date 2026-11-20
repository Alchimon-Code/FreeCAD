// SPDX-License-Identifier: LGPL-2.1-or-later
#include "detail.hpp"
namespace pd::edit {
using namespace detail;
namespace {
// Path coordinates are (edge index + local fraction), never tessellation indices. Two visits to
// the same world point on a self-crossing polyline can be distinct cuts.
double fraction(const Stroke& edge, Vec2 p) {
    if ((p - at(edge, 0)).length() <= EPSILON)
        return 0;
    if ((p - at(edge, 1)).length() <= EPSILON)
        return 1;
    if (auto l = std::get_if<Segment>(&edge)) {
        auto v = l->b - l->a;
        return std::clamp(v.normalized().dot(p - l->a) / v.length(), 0., 1.);
    }
    if (auto r = std::get_if<Ring>(&edge))
        return directed({1, 0}, p - r->center, 1) / tau;
    auto& a = std::get<CircularArc>(edge);
    return std::clamp(directed(a.pointAt(0) - a.center(), p - a.center(), a.sweep()) / std::abs(a.sweep()),
                      0., 1.);
}
std::unique_ptr<Entity> piece(const Entity& src, const std::vector<Stroke>& path, double lo, double hi) {
    auto first = static_cast<size_t>(std::floor(lo)), end = static_cast<size_t>(std::ceil(hi));
    if (auto poly = dynamic_cast<const Polyline*>(&src)) {
        std::vector<Vertex> vertices;
        Vec2 last;
        for (size_t j = first; j < end; ++j) {
            auto& edge = path[j % path.size()];
            double a = std::max(0., lo - j), b = std::min(1., hi - j);
            double bulge = 0;
            if (a == 0 && b == 1)
                bulge = poly->vertices()[j % path.size()].bulge;
            else if (auto arc = std::get_if<CircularArc>(&edge))
                bulge = std::tan(arc->sweep() * (b - a) / 4);
            vertices.push_back({at(edge, a), bulge});
            last = at(edge, b);
        }
        vertices.push_back({last, 0});
        return std::make_unique<Polyline>(std::move(vertices));
    }
    const auto& s = path.front();
    if (auto r = std::get_if<Ring>(&s))
        return std::make_unique<Arc>(CircularArc(r->center, r->radius, lo * tau, (hi - lo) * tau));
    if (auto a = std::get_if<CircularArc>(&s))
        return std::make_unique<Arc>(
            CircularArc(a->center(), a->radius(), a->startAngle() + a->sweep() * lo, (hi - lo) * a->sweep()));
    return std::make_unique<Line>(at(s, lo), at(s, hi));
}
template <class F> void boundaries(const Document& d, Selection ids, EntityId target, F f) {
    require(!ids.empty(), "Select cutting or boundary edges.");
    for (auto id : ids) {
        require(id != target, "The target cannot also be a boundary.");
        require(visitEdges(entity(d, id), f), "Unsupported boundary entity.");
    }
}
bool within(const Stroke& s, Vec2 p) {
    if (auto l = std::get_if<Segment>(&s)) {
        auto v = l->b - l->a;
        double t = (p - l->a).dot(v.normalized());
        return t >= -EPSILON && t <= v.length() + EPSILON;
    }
    if (auto a = std::get_if<CircularArc>(&s))
        return a->containsAngle(angle(p - a->center()));
    return true;
}
} // namespace
Plan trim(const Document& d, EntityId id, Selection cutters, Vec2 pick) {
    const auto& e = entity(d, id);
    const auto path = edges(e);
    auto poly = dynamic_cast<const Polyline*>(&e);
    bool closed = dynamic_cast<const Circle*>(&e) || (poly && poly->closed());
    const double n = static_cast<double>(path.size());
    std::vector<double> stops;
    boundaries(d, cutters, id, [&](const Stroke& blade) {
        auto box = boundsOf(blade);
        for (size_t i = 0; i < path.size(); ++i) {
            if (!boundsOf(path[i]).intersects(box))
                continue;
            intersections(path[i], blade, [&](Vec2 p) {
                double t = i + fraction(path[i], p);
                if (!closed && (t == 0 || t == n))
                    return;
                stops.push_back(closed && t == n ? 0 : t);
            });
        }
    });
    std::sort(stops.begin(), stops.end());
    std::vector<double> cuts;
    auto point = [&](double t) {
        auto i = static_cast<size_t>(t);
        return at(path[i % path.size()], t - i);
    };
    for (double t : stops)
        if (cuts.empty() || (t != cuts.back() && (std::floor(t) != std::floor(cuts.back()) ||
                                                  (point(t) - point(cuts.back())).length() > EPSILON)))
            cuts.push_back(t);
    require(cuts.size() >= (closed ? 2u : 1u), "Not enough distinct intersections to trim.");
    auto i = nearest(path, pick);
    double t = i + fraction(path[i], closestOn(path[i], pick));
    auto after = std::upper_bound(cuts.begin(), cuts.end(), t);
    Plan plan(d);
    EntityId out = id;
    auto keep = [&](double a, double b) {
        plan.put(out, styled(e, piece(e, path, a, b)));
        out = 0;
    };
    if (closed) {
        double lo = after == cuts.begin() ? cuts.back() - n : after[-1],
               hi = after == cuts.end() ? cuts.front() + n : *after;
        if (hi >= n) {
            lo -= n;
            hi -= n;
        }
        keep(hi, lo + n);
    } else {
        if (after != cuts.begin())
            keep(0, after[-1]);
        if (after != cuts.end())
            keep(*after, n);
    }
    return plan;
}
Plan extend(const Document& d, EntityId id, Selection bounds, Vec2 pick) {
    const auto& e = entity(d, id);
    auto l = dynamic_cast<const Line*>(&e);
    auto a = dynamic_cast<const Arc*>(&e);
    require(l || a, "Extend requires a line or arc.");
    require(pick.finite(), "Invalid pick point.");
    const auto edge = edges(e).front();
    auto start = at(edge, 0), end = at(edge, 1);
    bool first = (start - pick).length() < (end - pick).length();
    auto from = first ? start : end;
    auto u = l ? (first ? start - end : end - start).normalized() : Vec2{};
    double best = std::numeric_limits<double>::infinity();
    Vec2 dest;
    boundaries(d, bounds, id, [&](const Stroke& boundary) {
        auto hit = [&](Vec2 p) {
            if (!within(boundary, p))
                return;
            double travel;
            if (l) {
                travel = u.dot(p - from);
                if (travel <= EPSILON)
                    return;
            } else {
                auto& arc = a->curve();
                double sign = (first ? -1 : 1) * arc.sweep();
                travel = directed(from - arc.center(), p - arc.center(), sign);
                if (travel * arc.radius() <= EPSILON ||
                    (tau - travel - std::abs(arc.sweep())) * arc.radius() <= EPSILON)
                    return;
            }
            if (travel < best) {
                best = travel;
                dest = p;
            }
        };
        auto r = intersectSupports(edge, boundary);
        using K = Intersection::Kind;
        if (r.kind == K::OnePoint || r.kind == K::TwoPoints)
            hit(r.first);
        if (r.kind == K::TwoPoints)
            hit(r.second);
        if (r.kind == K::Coincident && !std::holds_alternative<Ring>(boundary)) {
            hit(at(boundary, 0));
            hit(at(boundary, 1));
        }
    });
    require(std::isfinite(best), "No boundary in the extension direction.");
    std::unique_ptr<Entity> result;
    if (l)
        result = std::make_unique<Line>(first ? dest : start, first ? end : dest);
    else {
        auto& arc = a->curve();
        auto add = std::copysign(best, arc.sweep());
        result = std::make_unique<Arc>(
            CircularArc(arc.center(), arc.radius(), arc.startAngle() - (first ? add : 0), arc.sweep() + add));
    }
    Plan plan(d);
    plan.put(id, styled(e, std::move(result)));
    return plan;
}
} // namespace pd::edit
