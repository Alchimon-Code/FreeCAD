// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/entities/annotation.hpp>
#include <planedraft/entities/block.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace pd {
BlockDefinition::BlockDefinition(BlockId id, std::string name,
                                 std::vector<std::unique_ptr<const Entity>> members)
    : id_(id), name_(std::move(name)), members_(std::move(members)), memberIndex_(makeRTree()) {
    if (name_.empty() || members_.empty()) {
        throw std::invalid_argument("empty block definition");
    }
    size_t slot = 0;
    for (const auto& entity : members_) {
        ++count_;
        if (auto ref = dynamic_cast<const BlockReference*>(entity.get())) {
            count_ += ref->definition().expandedSize();
            depth_ = std::max(depth_, 1 + ref->definition().depth());
        }
        if (auto h = dynamic_cast<const Hatch*>(entity.get()))
            count_ += h->primitives().size();
        if (auto d = dynamic_cast<const Dimension*>(entity.get()))
            count_ += d->primitives().size();
        if (count_ > 200000 || depth_ > 64)
            throw std::length_error("expanded block size/depth limit");
        auto box = entity->getBounds();
        bounds_.include(box);
        // Construction points need not be inside the ink AABB (e.g. short arcs, insertions).
        for (auto p : entity->getSnapPoints(SnapType::Center | SnapType::Insertion))
            box.include(p);
        memberIndex_->insert(slot++, box);
    }
}

void BlockDefinition::visitMembers(const Matrix3x3& m, const BoundingBox2D* region,
                                   const std::function<void(const Entity&)>& visit) const {
    if (!region) {
        for (auto& member : members_)
            visit(*member);
        return;
    }
    auto hits = memberIndex_->query(*region, &m);
    std::sort(hits.begin(), hits.end());
    for (auto slot : hits)
        visit(*members_[static_cast<size_t>(slot)]);
}

BlockReference::BlockReference(std::shared_ptr<const BlockDefinition> block, Vec2 insertion, Vec2 scale,
                               double angle)
    : block_(std::move(block)), placement_(Matrix3x3::translation(insertion) * Matrix3x3::rotation(angle) *
                                           Matrix3x3::scaling(scale.x, scale.y)) {
    if (!block_ || !placement_.invertible()) {
        throw std::invalid_argument("invalid block insertion");
    }
    (void)getBounds();
}

BoundingBox2D BlockReference::getBounds() const { return placement_.applyBounds(block_->localBounds()); }
void BlockReference::transform(const Matrix3x3& m) {
    auto placement = m * placement_;
    if (!placement.invertible()) {
        throw std::invalid_argument("singular block insertion");
    }
    (void)placement.applyBounds(block_->localBounds());
    placement_ = placement;
}

std::unique_ptr<Entity> BlockReference::clone() const { return std::make_unique<BlockReference>(*this); }
std::vector<Vec2> BlockReference::getSnapPoints(SnapType mask) const {
    std::vector<Vec2> points;
    if (hasSnap(mask, SnapType::Insertion))
        points = getGripPoints();
    for (auto& e : block_->members())
        if (e->traits.isVisible)
            for (auto p : e->getSnapPoints(mask))
                points.push_back(placement_.applyPoint(p));
    return points;
}

std::vector<Vec2> BlockReference::getGripPoints() const { return {placement_.applyPoint({})}; }
} // namespace pd
