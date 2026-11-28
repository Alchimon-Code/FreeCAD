// SPDX-License-Identifier: LGPL-2.1-or-later
#include "tessellation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace pd {
namespace {
// For |sweep| <= pi, every point of the arc lies in the chord AABB expanded by its sagitta.
// Test before descending: zooming in on one inch of a kilometre-radius ring costs work only
// along the visible branches, not O(sqrt(radius * zoom)) for the entire ring.
template <class Evaluate, class Output>
void subdivide(double radius, double sweep, double lo, double hi, Vec2 a, Vec2 b, const Evaluate& at,
               double tolerance, const BoundingBox2D& clip, const Output& output) {
    const double sine = std::sin(0.25 * (hi - lo) * sweep);
    const double sag = radius * (2 * sine * sine); // stable form of r * (1 - cos(delta/2))
    if (std::max(a.x, b.x) + sag < clip.min().x || std::min(a.x, b.x) - sag > clip.max().x ||
        std::max(a.y, b.y) + sag < clip.min().y || std::min(a.y, b.y) - sag > clip.max().y) {
        return;
    }
    const double mid = std::midpoint(lo, hi);
    if (sag <= tolerance || mid == lo || mid == hi) {
        output(Segment{a, b});
        return;
    }
    const auto p = at(mid);
    if (a == b && a == p) { // smaller than the representable world resolution at this location
        output(Segment{a, b});
        return;
    }
    subdivide(radius, sweep, lo, mid, a, p, at, tolerance, clip, output);
    subdivide(radius, sweep, mid, hi, p, b, at, tolerance, clip, output);
}
} // namespace

double lodScale(double scale) { return std::exp2(std::ceil(std::log2(scale))); }
void tessellate(const Stroke& stroke, double scale, const BoundingBox2D& clip,
                const std::function<void(Segment)>& append) {
    auto output = [&](Segment s) {
        if (auto clipped = clipSegment(s, clip)) {
            append(*clipped);
        }
    };
    if (auto* s = std::get_if<Segment>(&stroke)) {
        output(*s);
        return;
    }
    auto curve = [&](double r, double sweep, auto at) {
        // Start with at most a quarter turn per chord (also gives a closed, nondegenerate distant ring).
        int n = static_cast<int>(std::ceil(std::abs(sweep) / (0.5 * std::numbers::pi)));
        Vec2 a = at(0.0);
        for (int i = 0; i < n; ++i) {
            const double lo = double(i) / n, hi = double(i + 1) / n;
            auto b = at(hi);
            subdivide(r, sweep, lo, hi, a, b, at, 0.35 / scale, clip, output);
            a = b;
        }
    };
    if (auto* ring = std::get_if<Ring>(&stroke)) {
        constexpr double tau = 2 * std::numbers::pi;
        curve(ring->radius, tau, [&](double t) {
            const double angle = t == 1 ? 0 : t * tau;
            return ring->center + Vec2{std::cos(angle), std::sin(angle)} * ring->radius;
        });
    } else {
        const auto& arc = std::get<CircularArc>(stroke);
        curve(arc.radius(), arc.sweep(), [&](double t) { return arc.pointAt(t); });
    }
}
} // namespace pd
