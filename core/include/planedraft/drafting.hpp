// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <map>
#include <planedraft/edit.hpp>
namespace pd {
class CircularReferenceError final : public std::invalid_argument {
  public:
    explicit CircularReferenceError(const std::string& name)
        : std::invalid_argument("circular block reference: " + name) {}
};
// Resolve forward references before immutable definitions are constructed; rejects cycles/deep graphs.
using BlockDependencies = std::map<std::string, std::vector<std::string>>;
std::vector<std::string> blockOrder(const BlockDependencies&);
using GraphicSink = std::function<void(const Entity&, const StrokeStyle&, LayerId)>;
// Leaf references last only for the duration of the callback. Null document: geometry only.
void visitGraphics(const Document*, const Entity&, const GraphicSink&, const Matrix3x3* placement = nullptr,
                   bool visibleOnly = true, bool includeInvisible = false,
                   const BoundingBox2D* window = nullptr);
void visitInsertFeatures(const Document&, const BlockReference&, SnapType, const std::function<void(Vec2)>&,
                         const BoundingBox2D* window = nullptr);
edit::Plan explode(const Document&, edit::Selection);
BlockId makeBlock(Document&, std::string name, Vec2 base, edit::Selection, LayerId = DefaultLayer);
// Emits page coordinates; the caller must intersect ALL ink (including text/fill) with clip.
using PageSink = std::function<void(const Entity&, const StrokeStyle&, LayerId, const BoundingBox2D& clip)>;
void visitPage(const Document&, const Layout&, const PageSink&);
} // namespace pd
