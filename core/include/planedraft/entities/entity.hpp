// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <planedraft/ids.hpp>
#include <planedraft/math/geometry.hpp>

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace pd {
struct Color {
    std::uint8_t r = 255, g = 255, b = 255;
    bool operator==(const Color&) const = default;
};
struct ByLayer {
    bool operator==(const ByLayer&) const = default;
};
struct ByBlock {
    bool operator==(const ByBlock&) const = default;
};
template <class T> using Property = std::variant<ByLayer, ByBlock, T>;
struct EntityTraits {
    LayerId layer = DefaultLayer;
    Property<Color> color;
    Property<std::string> lineType;
    Property<double> lineWeight; // millimetres
    bool isVisible = true;
    bool isSelected = false;
};

enum class SnapType : unsigned {
    None = 0,
    Endpoint = 1,
    Midpoint = 2,
    Center = 4,
    Quadrant = 8,
    Insertion = 16,
    Intersection = 32, // query-dependent, evaluated by SnapEngine
    Nearest = 64
};
constexpr SnapType operator|(SnapType a, SnapType b) {
    return static_cast<SnapType>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr bool hasSnap(SnapType mask, SnapType bit) {
    return (static_cast<unsigned>(mask) & static_cast<unsigned>(bit)) != 0;
}

class Transaction;
// Editable prototypes, immutable once published by Document. clone() preserves identity and traits.
class Entity {
  public:
    virtual ~Entity() = default;
    [[nodiscard]] EntityId id() const { return id_; }
    EntityTraits traits;
    [[nodiscard]] virtual BoundingBox2D getBounds() const = 0;
    virtual void transform(const Matrix3x3& mat) = 0;
    [[nodiscard]] virtual std::unique_ptr<Entity> clone() const = 0;
    [[nodiscard]] virtual std::vector<Vec2> getSnapPoints(SnapType mask) const = 0;
    [[nodiscard]] virtual std::vector<Vec2> getGripPoints() const = 0;

  private:
    friend class Transaction;
    EntityId id_ = 0; // unbound prototype
};
} // namespace pd
