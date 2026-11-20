// SPDX-License-Identifier: LGPL-2.1-or-later
#include <algorithm>
#include <cmath>
#include <planedraft/drafting.hpp>
#include <set>
namespace pd {
std::vector<std::string> blockOrder(const BlockDependencies& graph) {
    std::set<std::string> active, done;
    std::vector<std::string> order;
    auto visit = [&](auto&& self, const std::string& name) -> void {
        if (done.contains(name))
            return;
        if (!active.insert(name).second)
            throw CircularReferenceError(name);
        if (active.size() > 64)
            throw std::length_error("block nesting limit");
        auto it = graph.find(name);
        if (it == graph.end())
            throw std::invalid_argument("undefined block: " + name);
        for (auto& child : it->second)
            self(self, child);
        active.erase(name);
        done.insert(name);
        order.push_back(name);
    };
    for (auto& [name, _] : graph)
        visit(visit, name);
    return order;
}
namespace {
// Nonuniform INSERT can turn circular edges into ellipses. Keep INSERT analytic in the model/DXF;
// approximate just the affected output curve to 0.01 world units, never change its stored radius.
std::unique_ptr<Entity> stretched(const Entity& e, const Matrix3x3& m) {
    auto p = dynamic_cast<const Polyline*>(&e);
    std::optional<Polyline> temp;
    if (auto c = dynamic_cast<const Circle*>(&e))
        temp = Hatch::boundary(*c);
    if (auto a = dynamic_cast<const Arc*>(&e)) {
        auto& c = a->curve();
        temp.emplace(std::vector<Vertex>{{c.pointAt(0), std::tan(c.sweep() / 4)}, {c.pointAt(1), 0}});
    }
    if (temp)
        p = &*temp;
    if (!p)
        throw NotSimilarity{};
    double z = m.applyVector({1, 0}).length() + m.applyVector({0, 1}).length();
    std::vector<Vertex> vertices;
    for (auto v : polygonize(*p, .01 / z))
        vertices.push_back({m.applyPoint(v), 0});
    return std::make_unique<Polyline>(std::move(vertices), p->closed());
}
} // namespace
void visitGraphics(const Document* doc, const Entity& root, const GraphicSink& emit,
                   const Matrix3x3* placement, bool visibleOnly, bool includeInvisible,
                   const BoundingBox2D* window) {
    auto walk = [&](auto&& self, const Entity& e, const Matrix3x3* mat, const StrokeStyle* parent,
                    LayerId inherited, bool visible) -> void {
        auto layer = e.traits.layer ? e.traits.layer : inherited;
        auto l = doc ? doc->layer(layer) : nullptr;
        visible = visible && e.traits.isVisible;
        if ((!includeInvisible && !visible) || (visibleOnly && l && (!l->isVisible || l->isFrozen)))
            return;
        auto style = doc ? doc->resolveStyle(e, parent, l) : StrokeStyle{};
        if (auto b = dynamic_cast<const BlockReference*>(&e)) {
            auto m = mat ? *mat * b->placement() : b->placement();
            b->definition().visitMembers(
                m, window, [&](const Entity& member) { self(self, member, &m, &style, layer, visible); });
            return;
        }
        auto leaf = [&](const Entity& part) {
            if (window && !(mat ? mat->applyBounds(part.getBounds()) : part.getBounds()).intersects(*window))
                return;
            if (!mat && visible) {
                emit(part, style, layer);
                return;
            }
            auto copy = part.clone();
            try {
                if (mat)
                    copy->transform(*mat);
            } catch (const NotSimilarity&) {
                copy = stretched(part, *mat);
            }
            copy->traits.isVisible = visible;
            emit(*copy, style, layer);
        };
        if (auto d = dynamic_cast<const Dimension*>(&e)) {
            for (auto& p : d->primitives())
                leaf(*p);
        } else if (auto h = dynamic_cast<const Hatch*>(&e)) {
            for (auto& p : h->primitives())
                leaf(*p);
        } else
            leaf(e);
    };
    walk(walk, root, placement, nullptr, DefaultLayer, true);
}
void visitInsertFeatures(const Document& doc, const BlockReference& root, SnapType mode,
                         const std::function<void(Vec2)>& sink, const BoundingBox2D* window) {
    auto walk = [&](auto&& self, const Entity& e, const Matrix3x3& parent, LayerId inherited) -> void {
        auto layer = e.traits.layer ? e.traits.layer : inherited;
        auto l = doc.layer(layer);
        if (!e.traits.isVisible || !l || !l->isVisible || l->isFrozen)
            return;
        if (auto b = dynamic_cast<const BlockReference*>(&e)) {
            auto m = parent * b->placement();
            if (hasSnap(mode, SnapType::Insertion))
                sink(m.applyPoint({}));
            b->definition().visitMembers(m, window,
                                         [&](const Entity& member) { self(self, member, m, layer); });
        } else
            for (auto v : e.getSnapPoints(mode))
                sink(parent.applyPoint(v));
    };
    walk(walk, root, Matrix3x3{}, DefaultLayer);
}
edit::Plan explode(const Document& d, edit::Selection ids) {
    if (ids.empty())
        throw std::invalid_argument("select an insert, polyline or dimension");
    edit::Plan plan(d);
    std::set<EntityId> unique(ids.begin(), ids.end());
    for (auto id : unique) {
        auto e = d.entity(id);
        if (!dynamic_cast<const BlockReference*>(e) && !dynamic_cast<const Polyline*>(e) &&
            !dynamic_cast<const Dimension*>(e))
            throw std::invalid_argument("cannot explode entity");
        plan.put(id, {});
        visitGraphics(
            &d, *e,
            [&](const Entity& p, const StrokeStyle& s, LayerId l) {
                auto add = [&](std::unique_ptr<Entity> v) {
                    v->traits.isVisible = p.traits.isVisible;
                    v->traits.layer = l;
                    v->traits.color = s.color;
                    v->traits.lineWeight = s.lineWeight;
                    v->traits.lineType = s.lineType;
                    plan.put(0, std::move(v));
                };
                if (dynamic_cast<const Polyline*>(&p))
                    visitEdges(p, [&](const Stroke& edge) {
                        if (auto line = std::get_if<Segment>(&edge))
                            add(std::make_unique<Line>(line->a, line->b));
                        else
                            add(std::make_unique<Arc>(std::get<CircularArc>(edge)));
                    });
                else
                    add(p.clone());
            },
            nullptr, false, true);
    }
    return plan;
}
BlockId makeBlock(Document& d, std::string name, Vec2 base, edit::Selection ids, LayerId layer) {
    std::set<EntityId> unique(ids.begin(), ids.end());
    std::vector<std::unique_ptr<Entity>> owned;
    std::vector<const Entity*> members;
    for (auto id : unique) {
        auto e = d.entity(id);
        if (!e)
            throw std::invalid_argument("unknown block member");
        auto p = e->clone();
        p->transform(Matrix3x3::translation(-base));
        members.push_back(p.get());
        owned.push_back(std::move(p));
    }
    BlockId id = 0;
    d.transact([&](Transaction& t) {
        auto b = t.defineBlock(name, members);
        id = b->id();
        for (auto old : unique)
            t.remove(old);
        BlockReference ref(b, base);
        ref.traits.layer = layer;
        t.add(ref);
    });
    return id;
}
void visitPage(const Document& d, const Layout& l, const PageSink& emit) {
    l.validate();
    for (auto& vp : l.viewports) {
        auto transform = vp.modelToPaper();
        auto clip = vp.getBounds();
        auto ids = d.query(vp.modelWindow());
        std::sort(ids.begin(), ids.end());
        for (auto id : ids)
            visitGraphics(
                &d, *d.entity(id),
                [&](const Entity& e, const StrokeStyle& s, LayerId layer) { emit(e, s, layer, clip); },
                &transform, true, false, &clip);
    }
    BoundingBox2D page({}, l.size());
    for (auto& e : l.paperEntities)
        visitGraphics(&d, *e,
                      [&](const Entity& p, const StrokeStyle& s, LayerId layer) { emit(p, s, layer, page); });
    auto ids = d.layoutIds();
    auto it = std::find_if(ids.begin(), ids.end(), [&](auto id) { return d.layout(id)->name == l.name; });
    auto index = it == ids.end() ? 1 : 1 + std::distance(ids.begin(), it);
    for (auto& e : l.furniture(index, std::max<size_t>(1, ids.size())))
        emit(*e, d.resolveStyle(*e), DefaultLayer, page);
}
} // namespace pd
