// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/document.hpp>
#include <planedraft/entities/primitives.hpp>
#include <planedraft/pd_plugin_api.h>

namespace pd::abi {
Matrix3x3 matrix(PdMatrix m);
BoundingBox2D box(PdBox b);
std::unique_ptr<Entity> decode(const PdEntityData&);
Layer decode(const PdLayerData&);
// The root record, strings and arrays have one host-side lifetime. Never copy after construction.
struct EntitySnapshot {
    explicit EntitySnapshot(const Entity&);
    EntitySnapshot(const EntitySnapshot&) = delete;
    PdEntityData data{};
    std::string text, lineType;
    std::vector<PdVertex> vertices;
};
struct LayerSnapshot {
    explicit LayerSnapshot(const Layer&);
    LayerSnapshot(const LayerSnapshot&) = delete;
    PdLayerData data{};
    std::string name, lineType;
};
} // namespace pd::abi
