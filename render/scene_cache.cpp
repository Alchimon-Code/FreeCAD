// SPDX-License-Identifier: LGPL-2.1-or-later
#include "dash.hpp"
#include "scene_cache.hpp"
#include "tessellation.hpp"
#include <planedraft/drafting.hpp>

#include <algorithm>

namespace pd {
namespace {
bool gripInView(const Entity& e, const SelectionManager& selection, const BoundingBox2D& view) {
    if (!selection.selected(e)) {
        return false;
    }
    auto points = e.getGripPoints();
    return std::any_of(points.begin(), points.end(), [&](Vec2 p) { return view.contains(p); });
}
} // namespace
void SceneCache::invalidate(const Document& doc, std::span<const Change> changes,
                            const SelectionManager& selection) {
    for (const auto& change : changes) {
        if (change.kind == Change::Kind::Reset) {
            queryNeeded_ = true;
            for (auto& [_, page] : pages_)
                page.dirty = true; // even identical numeric IDs are now different objects
            continue;
        }
        if (change.object == Change::Object::Layer) {
            queryNeeded_ = true; // a layer may just have become visible
            for (auto& [key, page] : pages_) {
                page.dirty |= page.containsBlock || page.layers.contains(change.id);
            }
            continue;
        }
        if (change.object != Change::Object::Entity) {
            continue;
        }
        const auto key = change.id / chunkSize;
        auto it = pages_.find(key);
        const Entity* e = doc.entity(change.id); // null for Removed (including undo of Added)
        const bool visible = e && !view_.empty() && doc.visible(*e) &&
                             (view_.intersects(e->getBounds()) || gripInView(*e, selection, view_));
        if (!visible && it == pages_.end()) {
            continue;
        }
        auto& page = visible ? pages_[key] : it->second;
        auto pos = std::lower_bound(page.ids.begin(), page.ids.end(), change.id);
        const bool member = pos != page.ids.end() && *pos == change.id;
        page.dirty |= visible || member;
        if (visible && !member) {
            page.ids.insert(pos, change.id);
        } else if (!visible && member) {
            page.ids.erase(pos);
        }
    }
}
void SceneCache::invalidateSelection(std::span<const EntityId> changed) {
    queryNeeded_ |= !changed.empty(); // an off-stroke grip can enter or leave the visible set
    for (auto id : changed) {
        if (auto it = pages_.find(id / chunkSize); it != pages_.end()) {
            it->second.dirty = true;
        }
    }
}
void SceneCache::sync(const Document& doc, const Camera2D& camera, double dpr,
                      const SelectionManager& selection, const Upload& upload, const Evict& evict) {
    const auto view = camera.viewBounds(1);
    if (queryNeeded_ || view != view_ || modelFlags_ != selection.usesModelFlags()) {
        std::map<EntityId, std::vector<EntityId>> wanted;
        for (auto id : doc.query(view)) {
            wanted[id / chunkSize].push_back(id);
        }
        if (selection.usesModelFlags() || !selection.ids().empty()) {
            auto extra = doc.queryExtraPoints(view);
            std::sort(extra.begin(), extra.end());
            extra.erase(std::unique(extra.begin(), extra.end()), extra.end());
            for (auto id : extra) {
                if (gripInView(*doc.entity(id), selection, view)) {
                    wanted[id / chunkSize].push_back(id);
                }
            }
        }
        for (auto& [key, page] : pages_) {
            page.dirty |= page.usedModelFlag && !selection.usesModelFlags();
            if (!wanted.contains(key)) {
                page.ids.clear();
            }
        }
        for (auto& [key, ids] : wanted) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            auto& page = pages_[key];
            if (ids != page.ids) {
                page.ids.swap(ids);
                page.dirty = true;
            }
        }
        queryNeeded_ = false;
        view_ = view;
        modelFlags_ = selection.usesModelFlags();
    }
    const auto scale = camera.scale() * dpr, lod = lodScale(scale);
    std::vector<RenderVertex> vertices, fills, grips;
    for (auto it = pages_.begin(); it != pages_.end();) {
        auto& [key, page] = *it;
        if (page.ids.empty()) {
            evict(key);
            it = pages_.erase(it);
            continue;
        }
        ++it;
        if (!page.dirty && page.coverage.contains(view) && (page.curveLod == 0 || page.curveLod == lod) &&
            scale <= page.precisionLimit) {
            continue;
        }
        vertices.clear();
        grips.clear();
        fills.clear();
        page.containsBlock = false;
        page.layers.clear();
        page.curveLod = 0;
        page.usedModelFlag = false;
        const double extent = std::max(camera.size().x, camera.size().y);
        page.coverage = camera.viewBounds(2 + extent * 0.5); // guard band for panning without re-upload
        // Rebase even a straight page when its old float offsets would span too many pixels at high zoom.
        page.precisionLimit = std::max(scale, 65536 * camera.scale() / (extent + 2));
        auto vertex = [&](Vec2 p, Ink ink) {
            auto v = Camera2D::relativeToEye(p, camera.eye());
            return RenderVertex{v[0], v[1], ink};
        };
        for (auto id : page.ids) {
            const auto& e = *doc.entity(id);
            page.layers.insert(e.traits.layer);
            page.containsBlock |= dynamic_cast<const BlockReference*>(&e) != nullptr;
            bool selected = selection.selected(e);
            page.usedModelFlag |= selected && selection.usesModelFlags();
            if (page.coverage.intersects(e.getBounds()))
                visitGraphics(
                    &doc, e,
                    [&](const Entity& leaf, const StrokeStyle& style, LayerId layer) {
                        page.layers.insert(layer);
                        auto c = style.color;
                        auto ink = selected ? selectionInk : Ink{c.r, c.g, c.b};
                        if (auto f = dynamic_cast<const SolidFace*>(&leaf)) {
                            clipFace(*f, page.coverage, [&](Vec2 p) { fills.push_back(vertex(p, ink)); });
                            return;
                        }
                        // A dashed or dotted linetype is split along the curve; the phase runs from one
                        // segment of the entity to the next, so the pattern stays even all the way round.
                        // Glyph outlines of a text keep their own solid ink.
                        const bool plain = dynamic_cast<const Text*>(&leaf) != nullptr;
                        Dasher dasher(plain ? std::string_view{} : std::string_view(style.lineType));
                        auto emitLine = [&](Segment s) {
                            vertices.push_back(vertex(s.a, ink));
                            vertices.push_back(vertex(s.b, ink));
                        };
                        visitStrokes(
                            leaf,
                            [&](const Stroke& stroke) {
                                if (!std::holds_alternative<Segment>(stroke))
                                    page.curveLod = lod;
                                tessellate(stroke, lod, page.coverage, [&](Segment s) {
                                    if (dasher.solid())
                                        emitLine(s);
                                    else
                                        dasher.split(s, emitLine);
                                });
                            },
                            text_);
                    },
                    nullptr, true, false, &page.coverage);
            if (selected) {
                for (auto p : e.getGripPoints()) {
                    if (page.coverage.contains(p)) {
                        grips.push_back(vertex(p, gripInk));
                    }
                }
            }
        }
        const auto lines = vertices.size();
        vertices.insert(vertices.end(), fills.begin(), fills.end());
        vertices.insert(vertices.end(), grips.begin(), grips.end());
        upload(key, camera.eye(), vertices, lines, fills.size());
        page.dirty = false;
    }
}
} // namespace pd
