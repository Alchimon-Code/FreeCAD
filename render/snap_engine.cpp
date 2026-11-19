// SPDX-License-Identifier: LGPL-2.1-or-later
#include "snap_engine.hpp"
#include <planedraft/drafting.hpp>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace pd {
std::optional<SnapHit> SnapEngine::find(const Document& doc, const Camera2D& camera, Vec2 screen,
                                        double radiusPixels, SnapType mask, EntityId exclude) const {
    return search(doc, camera, screen, radiusPixels, mask, exclude, false);
}
std::optional<SnapHit> SnapEngine::pick(const Document& doc, const Camera2D& cam, Vec2 screen,
                                        double radius) const {
    return search(doc, cam, screen, radius, SnapType::Nearest, 0, true);
}
std::optional<SnapHit> SnapEngine::search(const Document& doc, const Camera2D& camera, Vec2 screen,
                                          double radiusPixels, SnapType mask, EntityId exclude,
                                          bool pickFill) const {
    if (!screen.finite() || !std::isfinite(radiusPixels) || radiusPixels < 0 || mask == SnapType::None) {
        return {};
    }
    const auto cursor = camera.screenToWorld(screen);
    const double radius = radiusPixels / camera.scale();
    const BoundingBox2D box(cursor - Vec2{radius, radius}, cursor + Vec2{radius, radius});
    auto ids = doc.query(box);
    if (exclude)
        std::erase(ids, exclude);
    std::sort(ids.begin(), ids.end());

    std::optional<SnapHit> feature, nearest;
    auto consider = [&](Vec2 p, SnapType type, EntityId id, EntityId other = 0) {
        const double dist = (cursor - p).length() * camera.scale();
        auto& best = type == SnapType::Nearest ? nearest : feature;
        // Prefer a discrete feature to its projection. Within a class choose the closest;
        // exact ties are stable, independent of tree traversal and insertion/undo order.
        auto rank = [](const SnapHit& h) {
            return std::tuple{h.pixels, h.type, h.id, h.other, h.point.x, h.point.y};
        };
        const SnapHit h{p, type, id, other, dist};
        if (dist <= radiusPixels && (!best || rank(h) < rank(*best))) {
            best = h;
        }
    };
    auto points = [&](const Entity& e, SnapType mode) {
        if (hasSnap(mask, mode)) {
            if (auto b = dynamic_cast<const BlockReference*>(&e)) {
                visitInsertFeatures(doc, *b, mode, [&](Vec2 p) { consider(p, mode, e.id()); }, &box);
                return;
            }
            for (auto p : e.getSnapPoints(mode)) {
                consider(p, mode, e.id());
            }
        }
    };
    if (hasSnap(mask, SnapType::Center) || hasSnap(mask, SnapType::Insertion)) {
        auto extra = doc.queryExtraPoints(box);
        std::sort(extra.begin(), extra.end());
        extra.erase(std::unique(extra.begin(), extra.end()), extra.end());
        for (auto id : extra) {
            if (id != exclude && !std::binary_search(ids.begin(), ids.end(), id)) {
                // Only the construction point is local. Never expand distant polyline edges or glyphs.
                const auto& e = *doc.entity(id);
                points(e, SnapType::Center);
                points(e, SnapType::Insertion);
            }
        }
    }
    struct LocalEdge {
        EntityId id;
        Stroke curve;
        BoundingBox2D bounds;
    };
    std::vector<LocalEdge> edges;
    for (auto id : ids) {
        const auto& e = *doc.entity(id);
        for (auto mode : {SnapType::Endpoint, SnapType::Midpoint, SnapType::Center, SnapType::Quadrant,
                          SnapType::Insertion}) {
            points(e, mode);
        }
        if (!(hasSnap(mask, SnapType::Nearest) || hasSnap(mask, SnapType::Intersection))) {
            continue;
        }
        auto edge = [&](const Stroke& c) {
            auto bounds = boundsOf(c);
            if (!bounds.intersects(box)) {
                return;
            }
            auto p = closestOn(c, cursor);
            if ((p - cursor).length() > radius) {
                return;
            }
            if (hasSnap(mask, SnapType::Nearest)) {
                consider(p, SnapType::Nearest, id);
            }
            if (hasSnap(mask, SnapType::Intersection)) {
                for (const auto& previous : edges) {
                    if ((previous.id != id || dynamic_cast<const Polyline*>(&e) ||
                         dynamic_cast<const BlockReference*>(&e)) &&
                        previous.bounds.intersects(bounds)) {
                        intersections(previous.curve, c,
                                      [&](Vec2 q) { consider(q, SnapType::Intersection, previous.id, id); });
                    }
                }
                edges.push_back({id, c, bounds});
            }
        };
        visitGraphics(
            &doc, e,
            [&](const Entity& leaf, const StrokeStyle&, LayerId) {
                if (pickFill)
                    if (auto f = dynamic_cast<const SolidFace*>(&leaf); f && f->contains(cursor)) {
                        consider(cursor, SnapType::Nearest, id);
                        return;
                    }
                visitStrokes(leaf, edge, text_);
            },
            nullptr, true, false, &box);
    }
    return feature ? feature : nearest;
}
} // namespace pd
