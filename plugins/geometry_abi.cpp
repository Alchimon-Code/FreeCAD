// SPDX-License-Identifier: LGPL-2.1-or-later
#include "geometry_abi.hpp"
#include "utf8.hpp"

#include <stdexcept>

namespace pd::abi {
namespace {
Vec2 point(PdPoint p) { return {p.x, p.y}; }
PdPoint point(Vec2 p) { return {p.x, p.y}; }
Color rgb(PdRgb c) { return {c.r, c.g, c.b}; }
PdRgb rgb(Color c) { return {c.r, c.g, c.b}; }
template <class T> Property<T> property(uint32_t mode, T value) {
    switch (mode) {
    case PD_BY_LAYER:
        return ByLayer{};
    case PD_BY_BLOCK:
        return ByBlock{};
    case PD_EXPLICIT:
        return value;
    default:
        throw std::invalid_argument("invalid property mode");
    }
}
template <class T> uint32_t mode(const Property<T>& p) {
    return std::holds_alternative<T>(p)         ? PD_EXPLICIT
           : std::holds_alternative<ByLayer>(p) ? PD_BY_LAYER
                                                : PD_BY_BLOCK;
}
const char* required(const char* p) {
    if (!p)
        throw std::invalid_argument("missing string");
    if (!validText(p))
        throw std::invalid_argument("invalid UTF-8 string");
    return p;
}
} // namespace
Matrix3x3 matrix(PdMatrix m) { return {m.a, m.b, m.tx, m.c, m.d, m.ty}; }
BoundingBox2D box(PdBox b) { return {point(b.min), point(b.max)}; }
std::unique_ptr<Entity> decode(const PdEntityData& d) {
    if (d.struct_size < PD_ENTITY_V2_MIN_SIZE || (d.flags & ~(PD_HIDDEN | PD_SELECTED)))
        throw std::invalid_argument("invalid entity descriptor");
    const auto& g = d.geometry;
    std::unique_ptr<Entity> e;
    switch (d.kind) {
    case PD_LINE:
        e = std::make_unique<Line>(point(g.line.start), point(g.line.end));
        break;
    case PD_CIRCLE:
        e = std::make_unique<Circle>(point(g.circle.center), g.circle.radius);
        break;
    case PD_ARC:
        e = std::make_unique<Arc>(CircularArc(point(g.arc.center), g.arc.radius, g.arc.start, g.arc.sweep));
        break;
    case PD_POLYLINE: {
        auto& p = g.polyline;
        if (!p.vertices || p.count < 2 || p.count > 16 * 1024 * 1024 || p.closed > 1)
            throw std::invalid_argument("invalid polyline array");
        std::vector<Vertex> v;
        v.reserve(static_cast<std::size_t>(p.count));
        for (size_t i = 0; i < p.count; ++i)
            v.push_back({point(p.vertices[i].point), p.vertices[i].bulge});
        e = std::make_unique<Polyline>(std::move(v), p.closed != 0);
        break;
    }
    case PD_TEXT:
        e = std::make_unique<Text>(required(g.text.utf8), box(g.text.local_ink), matrix(g.text.placement));
        break;
    default:
        throw std::domain_error("unsupported entity kind");
    }
    e->traits.layer = d.layer;
    e->traits.isVisible = !(d.flags & PD_HIDDEN);
    e->traits.isSelected = d.flags & PD_SELECTED;
    e->traits.color = property(d.color_mode, rgb(d.color));
    e->traits.lineWeight = property(d.weight_mode, d.weight);
    e->traits.lineType =
        property(d.linetype_mode, std::string(d.linetype_mode == PD_EXPLICIT ? required(d.linetype) : ""));
    return e;
}
Layer decode(const PdLayerData& d) {
    if (d.struct_size < PD_LAYER_V2_MIN_SIZE || (d.flags & ~(PD_HIDDEN | PD_FROZEN | PD_LOCKED)))
        throw std::invalid_argument("invalid layer descriptor");
    Layer l(required(d.name));
    l.color = rgb(d.color);
    l.lineType = required(d.linetype);
    l.lineWeight = d.weight;
    l.isVisible = !(d.flags & PD_HIDDEN);
    l.isLocked = d.flags & PD_LOCKED;
    l.isFrozen = d.flags & PD_FROZEN;
    return l;
}
EntitySnapshot::EntitySnapshot(const Entity& e) {
    auto& d = data;
    d.struct_size = sizeof d;
    d.layer = e.traits.layer;
    d.flags = (e.traits.isVisible ? 0 : PD_HIDDEN) | (e.traits.isSelected ? PD_SELECTED : 0);
    d.color_mode = mode(e.traits.color);
    if (auto c = std::get_if<Color>(&e.traits.color))
        d.color = rgb(*c);
    d.weight_mode = mode(e.traits.lineWeight);
    if (auto w = std::get_if<double>(&e.traits.lineWeight))
        d.weight = *w;
    d.linetype_mode = mode(e.traits.lineType);
    if (auto s = std::get_if<std::string>(&e.traits.lineType)) {
        lineType = *s;
        d.linetype = lineType.c_str();
    }
    auto& g = d.geometry;
    if (auto* l = dynamic_cast<const Line*>(&e)) {
        d.kind = PD_LINE;
        g.line = {point(l->start()), point(l->end())};
    } else if (auto* c = dynamic_cast<const Circle*>(&e)) {
        d.kind = PD_CIRCLE;
        g.circle = {point(c->center()), c->radius()};
    } else if (auto* a = dynamic_cast<const Arc*>(&e)) {
        d.kind = PD_ARC;
        const auto& c = a->curve();
        g.arc = {point(c.center()), c.radius(), c.startAngle(), c.sweep()};
    } else if (auto* p = dynamic_cast<const Polyline*>(&e)) {
        d.kind = PD_POLYLINE;
        vertices.reserve(p->vertices().size());
        for (auto v : p->vertices())
            vertices.push_back({point(v.point), v.bulge});
        g.polyline = {vertices.data(), vertices.size(), p->closed() ? 1u : 0u};
    } else if (auto* t = dynamic_cast<const Text*>(&e)) {
        d.kind = PD_TEXT;
        text = t->content();
        auto& m = t->placement();
        auto x = m.applyVector({1, 0}), y = m.applyVector({0, 1}), o = m.applyPoint({});
        auto b = t->localInk();
        g.text = {text.c_str(), {point(b.min()), point(b.max())}, {x.x, y.x, o.x, x.y, y.y, o.y}};
    } else
        throw std::domain_error("entity type not exposed in v2");
}
LayerSnapshot::LayerSnapshot(const Layer& l) : name(l.name), lineType(l.lineType) {
    data = {sizeof data,
            (l.isVisible ? 0u : PD_HIDDEN) | (l.isFrozen ? PD_FROZEN : 0u) | (l.isLocked ? PD_LOCKED : 0u),
            name.c_str(),
            rgb(l.color),
            lineType.c_str(),
            l.lineWeight};
}
} // namespace pd::abi
