// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "render_vertex.hpp"
#include "selection_manager.hpp"

#include <map>

namespace pd {
class SceneCache {
  public:
    static constexpr EntityId chunkSize = 256;
    using Upload = std::function<void(EntityId key, Vec2 origin, std::span<const RenderVertex>,
                                      std::size_t lineVertices, std::size_t fillVertices)>;
    using Evict = std::function<void(EntityId key)>;
    explicit SceneCache(TextStroker text = nullptr) : text_(text) {}
    void invalidate(const Document&, std::span<const Change> changes,
                    const SelectionManager&); // never touches GL
    void invalidateSelection(std::span<const EntityId> changed);
    void sync(const Document&, const Camera2D&, double dpr, const SelectionManager&, const Upload&,
              const Evict&);

  private:
    struct Page {
        std::vector<EntityId> ids; // sorted, visible only
        std::set<LayerId> layers;
        BoundingBox2D coverage;
        double curveLod = 0, precisionLimit = 0;
        bool dirty = true, usedModelFlag = false, containsBlock = false;
    };
    std::map<EntityId, Page> pages_;
    BoundingBox2D view_;
    bool queryNeeded_ = true, modelFlags_ = true;
    TextStroker text_;
};
} // namespace pd
