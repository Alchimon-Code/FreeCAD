// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "camera2d.hpp"
#include "strokes.hpp"
#include <planedraft/document.hpp>

#include <set>

namespace pd {
// Editor state, deliberately not a Document edit/undo step. Never retains Entity pointers.
class SelectionManager {
  public:
    using Ids = std::set<EntityId>;
    explicit SelectionManager(TextStroker text = nullptr) : text_(text) {}
    // Before the first explicit selection, display imported selection flags lazily (no initial DOM scan).
    [[nodiscard]] bool usesModelFlags() const { return useModelFlags_; }
    [[nodiscard]] bool selected(const Entity& e) const {
        return useModelFlags_ ? e.traits.isSelected : ids_.contains(e.id());
    }
    [[nodiscard]] const Ids& ids() const { return ids_; }
    [[nodiscard]] Ids frame(const Document&, const Camera2D&, Vec2 first, Vec2 last) const;
    // Symmetric difference is used to invalidate only affected chunks.
    std::vector<EntityId> replace(Ids next);
    void reconcile(const Document&, std::span<const Change> changes);

  private:
    bool useModelFlags_ = true;
    Ids ids_;
    TextStroker text_;
};
} // namespace pd
