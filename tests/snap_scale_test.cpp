// SPDX-License-Identifier: LGPL-2.1-or-later
#include "snap_engine.hpp"

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

// Reproducible CPU workload, no threshold tied to CI machine speed, no graphics context.
TEST(SnapScale, ResolveFeaturesInOneHundredThousandEntities) {
    using namespace pd;
    using Clock = std::chrono::steady_clock;
    constexpr int cells = 50000, probes = 10000;
    auto origin = [](int i) { return Vec2{1e7 + 24 * (i % 250), -1e7 + 24 * (i / 250)}; };
    Document doc;
    std::vector<EntityId> lines, circles;
    auto build = Clock::now();
    doc.transact([&](Transaction& tx) {
        for (int i = 0; i < cells; ++i) {
            auto p = origin(i);
            lines.push_back(tx.add(Line(p + Vec2{-8, 0}, p + Vec2{8, 0})));
            circles.push_back(tx.add(Circle(p + Vec2{1, 0}, 4)));
        }
    });
    auto built = Clock::now();
    ASSERT_EQ(doc.size(), 100000u);
    SnapEngine engine;
    Camera2D camera;
    camera.resize(1200, 800);
    std::vector<double> us;
    us.reserve(probes);
    for (int j = 0; j < probes; ++j) {
        const int cell = (j * 7993) % cells;
        const auto p = origin(cell);
        camera.setView(p, 20); // 6 px = 0.3 units; all five modes enabled in every actual find()
        const Vec2 offsets[] = {{-8, 0}, {0, 0}, {1, 0}, {5, 0}, {7, 0}};
        const SnapType types[] = {SnapType::Endpoint, SnapType::Midpoint, SnapType::Center,
                                  SnapType::Intersection, SnapType::Nearest};
        int k = j % 5;
        auto pixel = camera.worldToScreen(p + offsets[k]) + Vec2{0, 1.5};
        auto t = Clock::now();
        auto h = engine.find(doc, camera, pixel, 6);
        auto end = Clock::now();
        us.push_back(std::chrono::duration<double, std::micro>(end - t).count());
        ASSERT_TRUE(h);
        ASSERT_EQ(h->type, types[k]);
        ASSERT_EQ(h->point, p + offsets[k]);
        ASSERT_EQ(h->id, k == 2 ? circles[cell] : lines[cell]);
        if (k == 3) {
            ASSERT_EQ(h->other, circles[cell]);
        }
    }
    std::sort(us.begin(), us.end());
    std::cout << "100000 entities; build=" << std::chrono::duration<double>(built - build).count() << " s; "
              << probes << " SnapEngine::find calls (all modes), median=" << us[probes / 2]
              << " us, p95=" << us[probes * 95 / 100] << " us, p99=" << us[probes * 99 / 100] << " us\n";
}
