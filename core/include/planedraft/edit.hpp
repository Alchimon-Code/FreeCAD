// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/document.hpp>
#include <planedraft/entities/primitives.hpp>

namespace pd::edit {
// An owning proposal, tied to one document revision. Creating/discarding it cannot issue an ID,
// notify observers or change history. The overlay reads these same prototypes before commit.
class Plan {
  public:
    struct Item {
        EntityId id;
        std::unique_ptr<const Entity> after;
    }; // id 0: add; null after: erase
    explicit Plan(const Document& doc) : source_(&doc), revision_(doc.revision()) {}
    void put(EntityId, std::unique_ptr<Entity>);
    [[nodiscard]] std::span<const Item> items() const { return items_; }
    void apply(Document&) const;

  private:
    const Document* source_;
    std::uint64_t revision_;
    std::vector<Item> items_;
};
using Selection = std::span<const EntityId>;
Plan affine(const Document&, Selection, const Matrix3x3&, bool duplicate = false);
Plan move(const Document&, Selection, Vec2 displacement);
Plan copy(const Document&, Selection, Vec2 displacement);
Plan rotate(const Document&, Selection, Vec2 pivot, double radians);
Plan scale(const Document&, Selection, Vec2 pivot, double sx, double sy);
Plan mirror(const Document&, Selection, Vec2 axisA, Vec2 axisB);
// Mouse forms share the same world-space math with other non-widget clients.
double referenceSize(const Document&, Selection, Vec2 pivot);
Plan rotateToward(const Document&, Selection, Vec2 pivot, Vec2 toward);
Plan scaleTo(const Document&, Selection, Vec2 pivot, double reference, Vec2 toward);
Plan erase(const Document&, Selection);
Plan line(const Document&, Vec2 a, Vec2 b, LayerId layer = DefaultLayer);
// Pick identifies the interval to discard / end to extend / side on which to create the copy.
Plan trim(const Document&, EntityId target, Selection cutters, Vec2 pick);
Plan extend(const Document&, EntityId target, Selection boundaries, Vec2 pick);
Plan offset(const Document&, EntityId target, double distance, Vec2 side);
// Points identify the retained rays of the two supporting lines.
Plan fillet(const Document&, EntityId a, Vec2 keepA, EntityId b, Vec2 keepB, double radius);
// Grip index follows Entity::getGripPoints().
Plan grip(const Document&, EntityId, std::size_t index, Vec2 destination);
enum class Tracking { Free, Ortho, Polar };
struct Tracked {
    Vec2 point;
    bool guided;
};
Tracked constrain(Vec2 base, Vec2 candidate, Tracking mode, double polarStepRadians);
} // namespace pd::edit
