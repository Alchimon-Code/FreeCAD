// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "detail.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <planedraft/drafting.hpp>
namespace pd::io {
// Group once, not a scan of all entities per layer. Hidden layers are retained as switchable groups.
struct Sheet {
    struct Item {
        std::shared_ptr<const Entity> owned;
        const Entity* geometry;
        StrokeStyle style;
        std::optional<BoundingBox2D> clip;
        std::vector<double> dash; // the pattern of the linetype, in drawing units
    };
    std::map<LayerId, std::vector<Item>> layers;
    BoundingBox2D bounds;
    double maxWeight = 0;
    Sheet(const Document& d, IoResult& report, const Layout* layout)
        : bounds(layout ? BoundingBox2D({}, layout->size()) : d.extents()) {
        for (auto id : d.layerIds())
            layers[id];
        auto add = [&](const Entity& e, StrokeStyle style, LayerId id, bool borrow,
                       std::optional<BoundingBox2D> clip) {
            if (layout && style.color == Color{})
                style.color = Color{0, 0, 0};
            maxWeight = std::max(maxWeight, style.lineWeight);
            // A dash pattern belongs to the drawing. A line whose name it does not carry is written
            // continuous, and the report names the miss instead of repeating it per entity.
            auto dash = d.lineTypePattern(style.lineType);
            std::string name = style.lineType;
            for (char& c : name)
                if (c >= 'A' && c <= 'Z')
                    c += 32;
            const bool plain = name.empty() || name == "continuous" || name == "bylayer" ||
                               name == "byblock";
            if (!plain && dash.empty())
                report.warn(0, "LTYPE", "no dash definition for this linetype; exported as continuous");
            std::shared_ptr<const Entity> owned;
            if (!borrow)
                owned = e.clone();
            auto* ptr = borrow ? &e : owned.get();
            layers[id].push_back({std::move(owned), ptr, style, clip, {dash.begin(), dash.end()}});
        };
        if (layout)
            visitPage(d, *layout,
                      [&](const Entity& e, const StrokeStyle& st, LayerId id, const BoundingBox2D& clip) {
                          add(e, st, id, false, clip);
                      });
        else
            for (auto id : d.entityIds()) {
                auto root = d.entity(id);
                visitGraphics(
                    &d, *root,
                    [&](const Entity& e, const StrokeStyle& st, LayerId layer) {
                        add(e, st, layer, &e == root, {});
                    },
                    nullptr, false);
            }
        if (bounds.empty())
            bounds = {{0, 0}, {1, 1}};
    }
};
// Sink operates in CAD coordinates (including the closing bulge). No dependency on the screen LOD.
template <class Sink> void path(const Entity& e, Sink& s) {
    if (auto face = dynamic_cast<const SolidFace*>(&e)) {
        auto& v = face->vertices();
        s.move(v[0]);
        s.line(v[1]);
        s.line(v[2]);
        s.close();
    } else if (auto l = dynamic_cast<const Line*>(&e)) {
        s.move(l->start());
        s.line(l->end());
    } else if (auto c = dynamic_cast<const Circle*>(&e)) {
        CircularArc a(c->center(), c->radius(), 0, std::numbers::pi),
            b(c->center(), c->radius(), std::numbers::pi, std::numbers::pi);
        s.move(a.pointAt(0));
        s.arc(a);
        s.arc(b);
        s.close();
    } else if (auto a = dynamic_cast<const Arc*>(&e)) {
        s.move(a->curve().pointAt(0));
        s.arc(a->curve());
    } else if (auto p = dynamic_cast<const Polyline*>(&e)) {
        auto v = p->vertices();
        s.move(v.front().point);
        for (size_t i = 0; i < (p->closed() ? v.size() : v.size() - 1); ++i) {
            auto next = v[(i + 1) % v.size()].point;
            if (v[i].bulge != 0)
                s.arc(CircularArc::fromBulge(v[i].point, next, v[i].bulge));
            else
                s.line(next);
        }
        if (p->closed())
            s.close();
    } else
        throw Failure(IoStatus::Unsupported, "cannot export this entity type");
}
} // namespace pd::io
