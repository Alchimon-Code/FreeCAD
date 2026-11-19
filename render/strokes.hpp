// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/math/edges.hpp>
namespace pd {
class Document;
class SolidFace;
void clipFace(const SolidFace&, const BoundingBox2D&, const std::function<void(Vec2)>& vertex);
// The Qt adapter supplies the same glyph outlines to graphics and picking. No font dependency in core.
using TextStroker = void (*)(const Text&, const StrokeSink&);
void visitStrokes(const Entity& entity, const StrokeSink& emitStroke, TextStroker text = nullptr,
                  const Document* doc = nullptr);
bool crosses(const Stroke& stroke, const BoundingBox2D& box);
std::optional<Segment> clipSegment(Segment segment, const BoundingBox2D& box);
} // namespace pd
