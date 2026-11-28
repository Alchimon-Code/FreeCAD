// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/entities/entity.hpp>
#include <utility>

namespace pd {
struct StrokeStyle {
    Color color;
    std::string lineType;
    double lineWeight;
};
class Layer {
  public:
    explicit Layer(std::string displayName = "0") : name(std::move(displayName)) {}
    [[nodiscard]] LayerId id() const { return id_; }
    std::string name;
    Color color{};
    std::string lineType = "Continuous";
    double lineWeight = 0.25;
    bool isVisible = true, isFrozen = false, isLocked = false;

  private:
    friend class Transaction;
    LayerId id_ = DefaultLayer;
};
} // namespace pd
