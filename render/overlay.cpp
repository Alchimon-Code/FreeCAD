// SPDX-License-Identifier: LGPL-2.1-or-later
#include "overlay.hpp"
#include "tessellation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pd {
std::size_t buildOverlay(std::vector<RenderVertex>& out, const Camera2D& camera, const OverlayState& state,
                         const edit::Plan* ghost, TextStroker text, double dpr) {
    out.clear();
    if (!state.cursor) {
        return 0;
    }
    const auto cursor = *state.cursor;
    constexpr Ink green{68, 240, 115}, blue{75, 159, 255}, hair{216, 228, 235};
    auto vertex = [&](Vec2 p, Ink c) {
        out.push_back({static_cast<float>(p.x), static_cast<float>(p.y), c});
    };
    auto line = [&](Vec2 a, Vec2 b, Ink c) {
        vertex(a, c);
        vertex(b, c);
    };
    auto dashedLine = [&](Vec2 a, Vec2 b, Ink ink) {
        if (auto s = clipSegment({a, b}, {{0, 0}, camera.size()})) {
            auto d = s->b - s->a;
            auto len = d.length();
            for (double t = 0; t < len; t += 10)
                line(s->a + d * (t / len), s->a + d * (std::min(t + 6, len) / len), ink);
        }
    };
    auto rectangle = [&](Vec2 a, Vec2 b, Ink ink, bool dashed = false) {
        const Vec2 corners[]{a, {a.x, b.y}, b, {b.x, a.y}, a};
        for (int i = 0; i < 4; ++i) {
            auto start = corners[i], end = corners[i + 1];
            if (!dashed) {
                line(start, end, ink);
            } else
                dashedLine(start, end, ink);
        }
    };
    // Filled geometry first (single triangle pass), then marker/guide strokes.
    auto fillRect = [&](Vec2 a, Vec2 b, Ink ink) {
        for (auto p : {a, Vec2{a.x, b.y}, b, a, b, Vec2{b.x, a.y}})
            vertex(p, ink);
    };
    if (state.frameStart) {
        Ink fill = cursor.x < state.frameStart->x ? green : blue;
        fill.a = 36;
        fillRect(*state.frameStart, cursor, fill);
    }
    if (state.hot && camera.viewBounds(6).contains(*state.hot)) {
        auto p = camera.worldToScreen(*state.hot);
        fillRect(p - Vec2{4, 4}, p + Vec2{4, 4}, hotGripInk);
    }
    const auto triangles = out.size();
    if (state.frameStart) {
        bool crossing = cursor.x < state.frameStart->x;
        rectangle(*state.frameStart, cursor, crossing ? green : blue, crossing);
    }
    if (state.guideBase && state.effective) {
        auto a = camera.worldToScreen(*state.guideBase), b = camera.worldToScreen(*state.effective);
        auto dir = b - a;
        if (std::isfinite(dir.length()) && dir.length() > EPSILON) {
            auto u = dir.normalized();
            double reach = (b - camera.size() * .5).length() + camera.size().length();
            dashedLine(b - u * reach, b + u * reach, {83, 208, 175, 200});
        }
    }
    if (ghost) {
        auto clip = camera.viewBounds(1);
        for (auto& item : ghost->items())
            if (item.after && clip.intersects(item.after->getBounds()))
                visitStrokes(
                    *item.after,
                    [&](const Stroke& s) {
                        tessellate(s, camera.scale() * dpr, clip, [&](Segment l) {
                            line(camera.worldToScreen(l.a), camera.worldToScreen(l.b), {118, 218, 255, 168});
                        });
                    },
                    text);
    }
    constexpr double r = OverlayState::aperture;
    rectangle(cursor - Vec2{r, r}, cursor + Vec2{r, r}, hair);
    line({0, cursor.y}, cursor - Vec2{r, 0}, hair);
    line(cursor + Vec2{r, 0}, {camera.size().x, cursor.y}, hair);
    line({cursor.x, 0}, cursor - Vec2{0, r}, hair);
    line(cursor + Vec2{0, r}, {cursor.x, camera.size().y}, hair);
    if (state.snap && !state.frameStart) {
        auto p = camera.worldToScreen(state.snap->point);
        constexpr double k = 6;
        const Vec2 a{k, k}, b{-k, k};
        switch (state.snap->type) {
        case SnapType::Endpoint:
            rectangle(p - a, p + a, green);
            break;
        case SnapType::Midpoint:
            line(p + b, p + a, green);
            line(p + a, p - Vec2{0, k}, green);
            line(p - Vec2{0, k}, p + b, green);
            break;
        case SnapType::Center:
            for (int i = 0; i < 24; ++i) {
                auto at = [&](int j) {
                    double t = j * (2 * std::numbers::pi / 24);
                    return p + Vec2{std::cos(t), std::sin(t)} * k;
                };
                line(at(i), at(i + 1), green);
            }
            break;
        case SnapType::Intersection:
            line(p - a, p + a, green);
            line(p - b, p + b, green);
            break;
        case SnapType::Nearest:
            line(p + b, p + a, green);
            line(p + a, p - a, green);
            line(p - a, p - b, green);
            line(p - b, p + b, green);
            break;
        default:
            break;
        }
    }
    return triangles;
}
} // namespace pd
