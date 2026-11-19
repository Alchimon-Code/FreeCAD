// SPDX-License-Identifier: LGPL-2.1-or-later
#include "detail.hpp"

namespace pd::edit {
using namespace detail;
void Plan::put(EntityId id, std::unique_ptr<Entity> value) {
    require(id || value, "Invalid edit.");
    if (id)
        require(!source_->layer(entity(*source_, id).traits.layer)->isLocked, "Locked layer.");
    if (value) {
        auto layer = source_->layer(value->traits.layer);
        require(layer && !layer->isLocked, "Locked or missing layer.");
    }
    items_.push_back({id, std::move(value)});
}
void Plan::apply(Document& d) const {
    require(&d == source_ && d.revision() == revision_, "The drawing changed; restart the edit.");
    d.transact([&](Transaction& t) {
        for (auto& [id, e] : items_) {
            if (!e)
                t.remove(id);
            else if (id)
                t.replace(id, *e);
            else
                t.add(*e);
        }
    });
}
namespace {
std::vector<EntityId> unique(Selection ids) {
    require(!ids.empty(), "Empty selection.");
    std::vector<EntityId> v(ids.begin(), ids.end());
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}
Matrix3x3 about(Vec2 p, const Matrix3x3& m) {
    return Matrix3x3::translation(p) * m * Matrix3x3::translation(-p);
}
// Three-point circle in coordinates relative to A. The intermediate point selects the sweep,
// including major arcs and CW arcs, rather than always choosing the minor arc.
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
} // namespace
Plan affine(const Document& d, Selection ids, const Matrix3x3& m, bool duplicate) {
    require(m.invertible(), "Singular transformation.");
    Plan p(d);
    for (auto id : unique(ids)) {
        auto e = entity(d, id).clone();
        e->transform(m);
        p.put(duplicate ? 0 : id, std::move(e));
    }
    return p;
}
Plan move(const Document& d, Selection ids, Vec2 v) {
    require(v.length() > EPSILON, "Zero displacement.");
    return affine(d, ids, Matrix3x3::translation(v));
}
Plan copy(const Document& d, Selection ids, Vec2 v) {
    return affine(d, ids, Matrix3x3::translation(v), true);
}
Plan rotate(const Document& d, Selection ids, Vec2 p, double a) {
    return affine(d, ids, about(p, Matrix3x3::rotation(a)));
}
Plan scale(const Document& d, Selection ids, Vec2 p, double x, double y) {
    return affine(d, ids, about(p, Matrix3x3::scaling(x, y)));
}
Plan mirror(const Document& d, Selection ids, Vec2 a, Vec2 b) {
    return affine(d, ids, Matrix3x3::reflection(a, b - a));
}
double referenceSize(const Document& d, Selection ids, Vec2 pivot) {
    require(!ids.empty() && pivot.finite(), "Invalid scale reference.");
    double r = 0;
    for (auto id : ids) {
        auto box = entity(d, id).getBounds();
        Vec2 far{std::max(std::abs(pivot.x - box.min().x), std::abs(pivot.x - box.max().x)),
                 std::max(std::abs(pivot.y - box.min().y), std::abs(pivot.y - box.max().y))};
        r = std::max(r, far.length());
    }
    require(std::isfinite(r), "Invalid scale reference.");
    return r > EPSILON ? r : 1.;
}
Plan rotateToward(const Document& d, Selection ids, Vec2 pivot, Vec2 toward) {
    return rotate(d, ids, pivot, angle((toward - pivot).normalized()));
}
Plan scaleTo(const Document& d, Selection ids, Vec2 pivot, double reference, Vec2 toward) {
    require(std::isfinite(reference) && reference > EPSILON, "Invalid scale reference.");
    double factor = (toward - pivot).length() / reference;
    return scale(d, ids, pivot, factor, factor);
}
Plan erase(const Document& d, Selection ids) {
    Plan p(d);
    for (auto id : unique(ids))
        p.put(id, {});
    return p;
}
Plan line(const Document& d, Vec2 a, Vec2 b, LayerId layer) {
    Plan p(d);
    auto e = std::make_unique<Line>(a, b);
    e->traits.layer = layer;
    p.put(0, std::move(e));
    return p;
}
Plan grip(const Document& d, EntityId id, size_t i, Vec2 dest) {
    auto& e = entity(d, id);
    auto points = e.getGripPoints();
    require(i < points.size() && dest.finite(), "Invalid grip.");
    auto delta = dest - points[i];
    require(delta.length() > EPSILON, "Zero displacement.");
    std::unique_ptr<Entity> next;
    if (auto dim = dynamic_cast<const Dimension*>(&e))
        next = std::make_unique<Dimension>(dim->withPoint(i, dest));
    else if (auto l = dynamic_cast<const Line*>(&e); l && i < 2)
        next = std::make_unique<Line>(i == 0 ? dest : l->start(), i == 1 ? dest : l->end());
    else if (auto c = dynamic_cast<const Circle*>(&e); c && i != 0)
        next = std::make_unique<Circle>(c->center(), (dest - c->center()).length());
    else if (dynamic_cast<const Arc*>(&e) && i < 3) {
        points[i] = dest;
        next = std::make_unique<Arc>(through(points[0], points[2], points[1]));
    } else if (auto p = dynamic_cast<const Polyline*>(&e)) {
        std::vector<Vertex> v(p->vertices().begin(), p->vertices().end());
        size_t j = i / 2, k = (j + 1) % v.size();
        if (i % 2 == 0)
            v[j].point = dest;
        else if (v[j].bulge == 0) {
            v[j].point = v[j].point + delta;
            v[k].point = v[k].point + delta;
        } else {
            auto chord = (v[k].point - v[j].point).normalized();
            v[j].bulge = std::abs(chord.cross(dest - v[j].point)) <= EPSILON
                             ? 0
                             : std::tan(through(v[j].point, dest, v[k].point).sweep() / 4);
        }
        next = std::make_unique<Polyline>(std::move(v), p->closed());
    } else {
        next = e.clone();
        next->transform(Matrix3x3::translation(delta));
    }
    Plan plan(d);
    plan.put(id, styled(e, std::move(next)));
    return plan;
}
Tracked constrain(Vec2 base, Vec2 candidate, Tracking mode, double step) {
    require(base.finite() && candidate.finite(), "Invalid input point.");
    auto v = candidate - base;
    require(v.finite(), "Unrepresentable input vector.");
    if (mode == Tracking::Ortho)
        return {std::abs(v.x) >= std::abs(v.y) ? Vec2{candidate.x, base.y} : Vec2{base.x, candidate.y}, true};
    if (mode == Tracking::Polar) {
        require(std::isfinite(step) && step > 0 && step <= std::numbers::pi, "Invalid polar increment.");
        double a = angle(v), ray = std::round(a / step) * step;
        if (std::abs(ray - a) <= std::min(step / 2, tau / 72)) {
            auto u = heading(ray);
            auto projected = base + u * u.dot(v);
            require(projected.finite(), "Unrepresentable tracking projection.");
            return {projected, true};
        }
    }
    return {candidate, false};
}
} // namespace pd::edit
