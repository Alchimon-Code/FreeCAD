// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/ids.hpp>
#include <planedraft/math/geometry.hpp>

#include <memory>
#include <vector>

namespace pd {
// Broad phase only. Geometric index: one entry per live entity. Erase matches both ID and the exact old box.
class SpatialIndex {
  public:
    virtual ~SpatialIndex() = default;
    [[nodiscard]] virtual std::unique_ptr<SpatialIndex> fork() const = 0;
    [[nodiscard]] virtual BoundingBox2D bounds() const = 0; // cached union; empty if the tree is empty
    virtual void insert(EntityId id, const BoundingBox2D& box) = 0;
    virtual bool erase(EntityId id, const BoundingBox2D& box) = 0;
    // A block definition shares its local tree between all inserts. With a placement, test
    // transformed node bounds against the world region; no inverse or world-space rebuild.
    [[nodiscard]] virtual std::vector<EntityId> query(const BoundingBox2D& region,
                                                      const Matrix3x3* placement = nullptr) const = 0;
};
[[nodiscard]] std::unique_ptr<SpatialIndex> makeRTree();
} // namespace pd
