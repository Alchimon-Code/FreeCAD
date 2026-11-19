// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "strokes.hpp"

namespace pd {
// Upper scale of a reusable LOD bucket, in *physical* pixels / world unit.
double lodScale(double physicalScale);
// Emits clipped GL_LINES, not a strip with connections between disjoint visible runs.
void tessellate(const Stroke& stroke, double physicalScale, const BoundingBox2D& clip,
                const std::function<void(Segment)>& append);
} // namespace pd
