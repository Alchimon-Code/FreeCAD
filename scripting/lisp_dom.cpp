// SPDX-License-Identifier: LGPL-2.1-or-later
#include "lisp.hpp"
#include <planedraft/entities/primitives.hpp>

namespace pd::lisp {
namespace {
Vec2 xy(const Value& v) {
    const auto& s = v.list();
    arity(s, 2, 2);
    return {s[0].number(), s[1].number()};
}
Value point(Vec2 p) { return List{p.x, p.y}; }
Value ids(const std::vector<EntityId>& range) {
    List result;
    result.reserve(range.size());
    for (auto id : range)
        result.emplace_back(id);
    return result;
}
void field(List& record, std::string key, Value v) {
    record.emplace_back(List{std::move(key), std::move(v)});
}
Value entity(const Entity& e) {
    List r;
    field(r, "id", e.id());
    field(r, "layer", e.traits.layer);
    auto b = e.getBounds();
    field(r, "bounds", List{point(b.min()), point(b.max())});
    if (auto l = dynamic_cast<const Line*>(&e)) {
        field(r, "type", "line");
        field(r, "start", point(l->start()));
        field(r, "end", point(l->end()));
    } else if (auto c = dynamic_cast<const Circle*>(&e)) {
        field(r, "type", "circle");
        field(r, "center", point(c->center()));
        field(r, "radius", c->radius());
    } else if (auto a = dynamic_cast<const Arc*>(&e)) {
        const auto& c = a->curve();
        field(r, "type", "arc");
        field(r, "center", point(c.center()));
        field(r, "radius", c.radius());
        field(r, "start", c.startAngle());
        field(r, "sweep", c.sweep());
    } else if (auto p = dynamic_cast<const Polyline*>(&e)) {
        field(r, "type", "polyline");
        List vertices;
        for (auto v : p->vertices())
            vertices.emplace_back(List{v.point.x, v.point.y, v.bulge});
        field(r, "vertices", std::move(vertices));
        field(r, "closed", p->closed());
    } else if (auto t = dynamic_cast<const Text*>(&e)) {
        field(r, "type", "text");
        field(r, "text", t->content());
        auto ink = t->localInk();
        field(r, "ink", List{point(ink.min()), point(ink.max())});
        const auto& m = t->placement();
        auto u = m.applyVector({1, 0}), v = m.applyVector({0, 1}), o = m.applyPoint({});
        field(r, "placement", List{u.x, v.x, o.x, u.y, v.y, o.y});
    } else
        field(r, "type", "block_reference");
    return r;
}
Value layer(const Layer& l) {
    List r;
    field(r, "id", l.id());
    field(r, "name", l.name);
    field(r, "color", List{uint64_t(l.color.r), uint64_t(l.color.g), uint64_t(l.color.b)});
    field(r, "linetype", l.lineType);
    field(r, "weight", l.lineWeight);
    field(r, "visible", l.isVisible);
    field(r, "frozen", l.isFrozen);
    field(r, "locked", l.isLocked);
    return r;
}
Matrix3x3 matrix(const Value& v) {
    const auto& a = v.list();
    arity(a, 6, 6);
    return {a[0].number(), a[1].number(), a[2].number(), a[3].number(), a[4].number(), a[5].number()};
}
} // namespace
std::optional<Value> dom(std::string_view op, const List& a, ExtensionContext& ctx, Transaction& tx,
                         bool writing) {
    auto check = [&](size_t n, size_t max, bool write = false) {
        arity(a, n, max);
        a[0].document();
        if (write && !writing)
            throw std::logic_error("write requires with-transaction");
    };
    if (op == "selection" || op == "layers" || op == "entities") {
        check(1, 1);
        return ids(op == "selection" ? ctx.selection()
                   : op == "layers"  ? ctx.doc.layerIds()
                                     : ctx.doc.entityIds());
    }
    if (op == "query_box") {
        check(3, 4);
        return ids(ctx.doc.query({xy(a[1]), xy(a[2])}, a.size() == 3 || a[3].boolean()));
    }
    if (op == "layer") {
        check(2, 2);
        auto* l = tx.layer(a[1].integer());
        if (!l)
            throw std::out_of_range("no such layer");
        return layer(*l);
    }
    if (op == "entity") {
        check(2, 2);
        auto* e = tx.entity(a[1].integer());
        if (!e)
            throw std::out_of_range("no such entity");
        return entity(*e);
    }
    if (op == "add_layer") {
        check(2, 3, true);
        Layer l(a[1].string());
        if (a.size() == 3) {
            auto& c = a[2].list();
            arity(c, 3, 3);
            auto channel = [&](size_t i) {
                auto v = c[i].integer();
                if (v > 255)
                    throw std::invalid_argument("invalid color channel");
                return static_cast<uint8_t>(v);
            };
            l.color = {channel(0), channel(1), channel(2)};
        }
        return tx.addLayer(l);
    }
    auto add = [&](Entity&& e, size_t index) {
        if (a.size() > index)
            e.traits.layer = a[index].integer();
        return tx.add(e);
    };
    if (op == "add_line") {
        check(3, 4, true);
        return add(Line(xy(a[1]), xy(a[2])), 3);
    }
    if (op == "add_circle") {
        check(3, 4, true);
        return add(Circle(xy(a[1]), a[2].number()), 3);
    }
    if (op == "add_arc") {
        check(5, 6, true);
        return add(Arc(CircularArc(xy(a[1]), a[2].number(), a[3].number(), a[4].number())), 5);
    }
    if (op == "add_polyline") {
        check(3, 4, true);
        std::vector<Vertex> v;
        for (auto& p : a[1].list()) {
            auto& q = p.list();
            arity(q, 2, 3);
            v.push_back({{q[0].number(), q[1].number()}, q.size() == 3 ? q[2].number() : 0});
        }
        return add(Polyline(std::move(v), a[2].boolean()), 3);
    }
    if (op == "add_text") {
        check(5, 6, true);
        return add(Text(a[1].string(), {xy(a[2]), xy(a[3])}, matrix(a[4])), 5);
    }
    if (op == "transform") {
        check(3, 3, true);
        tx.transform(a[1].integer(), matrix(a[2]));
        return a[1];
    }
    if (op == "delete") {
        check(2, 2, true);
        tx.remove(a[1].integer());
        return true;
    }
    return {};
}
} // namespace pd::lisp
