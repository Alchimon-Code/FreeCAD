// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/spatial/spatial_index.hpp>

#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <random>

namespace {
using namespace pd;
TEST(RTree, SplitRelocateMergeAndCompareWithBruteForce) {
    auto tree = makeRTree();
    std::map<EntityId, BoundingBox2D> live;
    std::mt19937 rng(42317);
    auto scalar = [&] { return static_cast<double>(int(rng() % 2000) - 1000); };
    auto box = [&] {
        Vec2 p{scalar(), scalar()};
        return BoundingBox2D(p, p + Vec2{double(rng() % 90), double(rng() % 70)});
    };
    auto check = [&](const BoundingBox2D& region) {
        std::vector<EntityId> expected;
        for (const auto& [id, b] : live) {
            if (b.intersects(region)) {
                expected.push_back(id);
            }
        }
        auto actual = tree->query(region);
        std::sort(actual.begin(), actual.end());
        EXPECT_EQ(actual, expected);
    };
    for (EntityId i = 1; i <= 1500; ++i) {
        live[i] = i % 11 == 0 ? BoundingBox2D({0, 0}, {0, 0}) : box();
        tree->insert(i, live[i]);
    }
    check({{0, 0}, {0, 0}});
    check({});
    auto frozen = tree->fork();
    auto origin = frozen->query({{0, 0}, {0, 0}});
    for (auto& [id, b] : live) {
        if (id % 3 == 0) {
            ASSERT_TRUE(tree->erase(id, b));
            b = box();
            tree->insert(id, b);
        }
    }
    EXPECT_EQ(frozen->query({{0, 0}, {0, 0}}), origin);
    for (int i = 0; i < 100; ++i) {
        check(box());
    }
    for (auto it = live.begin(); it != live.end();) {
        ASSERT_TRUE(tree->erase(it->first, it->second));
        it = live.erase(it);
        if (live.size() % 17 == 0) {
            check(box());
        }
    }
    EXPECT_TRUE(tree->query({{-2000, -2000}, {2000, 2000}}).empty());
    EXPECT_FALSE(tree->erase(0, box()));
    tree->insert(3, {{-4, -3}, {1, 1}});
    EXPECT_EQ(tree->query({{1, 1}, {2, 2}}), std::vector<EntityId>{3});
}
} // namespace
