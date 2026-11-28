// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/entities/entity.hpp>

#include <functional>
#include <planedraft/spatial/spatial_index.hpp>
#include <span>

namespace pd {
class BlockDefinition {
  public:
    void visitMembers(const Matrix3x3& placement, const BoundingBox2D* window,
                      const std::function<void(const Entity&)>& visit) const;
    size_t expandedSize() const { return count_; }
    size_t depth() const { return depth_; }
    [[nodiscard]] BlockId id() const { return id_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] BoundingBox2D localBounds() const { return bounds_; }
    [[nodiscard]] std::span<const std::unique_ptr<const Entity>> members() const { return members_; }

  private:
    friend class Transaction;
    BlockDefinition(BlockId id, std::string name, std::vector<std::unique_ptr<const Entity>> members);
    BlockId id_;
    std::string name_;
    std::vector<std::unique_ptr<const Entity>> members_;
    BoundingBox2D bounds_;
    std::unique_ptr<SpatialIndex> memberIndex_;
    size_t count_ = 0, depth_ = 1;
};

class BlockReference final : public Entity {
  public:
    BlockReference(std::shared_ptr<const BlockDefinition> block, Vec2 insertion, Vec2 scale = {1, 1},
                   double rotation = 0);
    [[nodiscard]] const BlockDefinition& definition() const { return *block_; }
    [[nodiscard]] const Matrix3x3& placement() const { return placement_; }
    BoundingBox2D getBounds() const override;
    void transform(const Matrix3x3& mat) override;
    std::unique_ptr<Entity> clone() const override;
    std::vector<Vec2> getSnapPoints(SnapType mask) const override;
    std::vector<Vec2> getGripPoints() const override;

  private:
    std::shared_ptr<const BlockDefinition> block_;
    Matrix3x3 placement_;
};
} // namespace pd
