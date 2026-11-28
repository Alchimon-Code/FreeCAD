// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "render_vertex.hpp"
#include "snap_engine.hpp"
#include <planedraft/edit.hpp>

namespace pd {
struct OverlayState {
    static constexpr double aperture = 9;   // snap radius in logical pixels
    std::optional<Vec2> cursor, frameStart; // screen coordinates
    std::optional<SnapHit> snap;
    std::optional<Vec2> guideBase, effective, hot; // world coordinates
};
// First triangles (frame fill / solid hot grip), then lines. UI coordinates, never document edits.
std::size_t buildOverlay(std::vector<RenderVertex>& out, const Camera2D& camera, const OverlayState& state,
                         const edit::Plan* ghost = nullptr, TextStroker text = nullptr, double dpr = 1);
} // namespace pd
