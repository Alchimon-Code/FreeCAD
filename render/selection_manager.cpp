// SPDX-License-Identifier: LGPL-2.1-or-later
#include "selection_manager.hpp"

#include <algorithm>
#include <iterator>
#include <planedraft/drafting.hpp>

namespace pd {
SelectionManager::Ids SelectionManager::frame(const Document& doc, const Camera2D& camera, Vec2 first,
                                              Vec2 last) const {
    const bool window = first.x <= last.x;
    const auto box = BoundingBox2D::fromPoints(camera.screenToWorld(first), camera.screenToWorld(last));
    Ids result;
    for (auto id : doc.query(box)) {
        const auto& e = *doc.entity(id);
        bool match = window, any = false;
        // A window must contain *all visible ink*, not the conservative (possibly rotated)
        // insert box. For crossing, cull with the definition's local member R-tree.
        visitGraphics(
            &doc, e,
            [&](const Entity& leaf, const StrokeStyle&, LayerId) {
                if (box.contains(leaf.getBounds()) && !dynamic_cast<const Text*>(&leaf)) {
                    any = true;
                    if (!window)
                        match = true;
                    return;
                }
                if (!window)
                    if (auto f = dynamic_cast<const SolidFace*>(&leaf); f && f->contains(box.min())) {
                        match = any = true;
                        return;
                    }
                visitStrokes(
                    leaf,
                    [&](const Stroke& c) {
                        any = true;
                        match = window ? match && box.contains(boundsOf(c)) : match || crosses(c, box);
                    },
                    text_);
            },
            nullptr, true, false, window ? nullptr : &box);
        if (any && match) {
            result.insert(id);
        }
    }
    return result;
}
std::vector<EntityId> SelectionManager::replace(Ids next) {
    std::vector<EntityId> changed;
    std::set_symmetric_difference(ids_.begin(), ids_.end(), next.begin(), next.end(),
                                  std::back_inserter(changed));
    ids_.swap(next);
    useModelFlags_ = false;
    return changed;
}
void SelectionManager::reconcile(const Document& doc, std::span<const Change> changes) {
    bool layerChanged = false;
    for (const auto& change : changes) {
        if (change.object == Change::Object::Layer) {
            layerChanged = true;
        } else if (change.object == Change::Object::Entity && ids_.contains(change.id)) {
            auto* e = doc.entity(change.id);
            if (!e || !doc.visible(*e)) {
                ids_.erase(change.id);
            }
        }
    }
    if (layerChanged) {
        std::erase_if(ids_, [&](auto id) {
            auto* e = doc.entity(id);
            return !e || !doc.visible(*e);
        });
    }
}
} // namespace pd
