// SPDX-License-Identifier: LGPL-2.1-or-later
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <planedraft/edit.hpp>
namespace {
using namespace pd;
constexpr double pi = std::numbers::pi;
void near(Vec2 p, Vec2 q) {
    EXPECT_NEAR(p.x, q.x, EPSILON);
    EXPECT_NEAR(p.y, q.y, EPSILON);
}
struct Edit : testing::Test {
    Document d;
    template <class T> const T& get(EntityId id) const { return dynamic_cast<const T&>(*d.entity(id)); }
    EntityId line(Vec2 a, Vec2 b) { return d.add(Line(a, b)); }
    void indexed(EntityId id, Vec2 p, bool expect) {
        auto found = d.query({p, p});
        EXPECT_EQ(std::count(found.begin(), found.end(), id), expect ? 1 : 0);
    }
};
TEST_F(Edit, AffinePlansDoNotPublishOrConsumeHandlesAndApplyInOnePacket) {
    auto a = line({0, 0}, {8, 0}), b = d.add(Circle({3, 4}, 2));
    std::array ids{a, b, a};
    auto rev = d.revision();
    int packets = 0;
    auto watch = d.observe([&](auto changes) {
        ++packets;
        EXPECT_EQ(changes.size(), 2u);
    });
    for (int i = 0; i < 25; ++i) {
        auto ghost = edit::copy(d, ids, {double(i), 0});
        EXPECT_EQ(ghost.items().size(), 2u);
    }
    EXPECT_EQ(d.revision(), rev);
    EXPECT_EQ(packets, 0);
    auto copy = edit::copy(d, ids, {20, 3});
    copy.apply(d);
    EXPECT_EQ(d.entityIds(), (std::vector<EntityId>{1, 2, 3, 4}));
    near(get<Line>(3).start(), {20, 3});
    near(get<Circle>(4).center(), {23, 7});
    EXPECT_EQ(packets, 1);
    EXPECT_THROW(copy.apply(d), std::invalid_argument);
    ASSERT_TRUE(d.undo());
    EXPECT_FALSE(d.entity(3));
    ASSERT_TRUE(d.redo());
    near(get<Line>(3).end(), {28, 3});
    edit::move(d, ids, {2, -1}).apply(d);
    near(get<Circle>(b).center(), {5, 3});
    edit::rotate(d, ids, {2, -1}, pi / 2).apply(d);
    near(get<Line>(a).end(), {2, 7});
    edit::scale(d, ids, {2, -1}, 2, 2).apply(d);
    near(get<Line>(a).end(), {2, 15});
    EXPECT_DOUBLE_EQ(get<Circle>(b).radius(), 4);
    auto before = d.revision();
    EXPECT_THROW(edit::scale(d, ids, {}, 1, 2), std::invalid_argument);
    EXPECT_EQ(d.revision(), before);
    indexed(a, {2, 15}, true);
}
TEST_F(Edit, MirrorPreservesTextPlacementAndReversesDirectedCurves) {
    auto a = d.add(Arc(CircularArc({4, 2}, 3, 0, -pi / 2))),
         b = d.add(Polyline({{{1, 1}, -.5}, {{6, 2}, 0}}));
    Text t("А", {{0, 0}, {2, 1}}, Matrix3x3::translation({4, 3}));
    auto c = d.add(t);
    std::array ids{a, b, c};
    edit::mirror(d, ids, {1, -1}, {1, 2}).apply(d);
    auto& arc = get<Arc>(a).curve();
    near(arc.center(), {-2, 2});
    near(arc.pointAt(0), {-5, 2});
    EXPECT_NEAR(arc.sweep(), pi / 2, EPSILON);
    EXPECT_DOUBLE_EQ(get<Polyline>(b).vertices()[0].bulge, .5);
    near(get<Text>(c).placement().applyPoint({1, 1}), {-3, 4});
}
TEST_F(Edit, TrimLineBetweenMultipleFiniteCuttersSplitsAndRetainsPropertiesAndOldId) {
    Line l({-10, 0}, {10, 0});
    l.traits.color = Color{12, 35, 90};
    auto id = d.add(l);
    std::array blades{line({-2, -5}, {-2, 5}), line({4, -5}, {4, 5}), line({0, 2}, {0, 7})};
    auto removed = edit::trim(d, id, blades, {1, 0});
    auto rev = d.revision();
    indexed(id, {1, 0}, true);
    removed.apply(d);
    EXPECT_EQ(d.revision(), rev + 1);
    EXPECT_EQ(d.size(), 5u);
    near(get<Line>(id).end(), {-2, 0});
    auto other = d.entityIds().back();
    near(get<Line>(other).start(), {4, 0});
    EXPECT_EQ(get<Line>(other).traits.color, l.traits.color);
    indexed(id, {1, 0}, false);
    indexed(other, {9, 0}, true);
    EXPECT_TRUE(d.undo());
    indexed(id, {1, 0}, true);
    EXPECT_EQ(d.size(), 4u);
}
TEST_F(Edit, TrimCircleAcrossParameterSeamBecomesArcUndoRestoresCircleAndIndex) {
    Circle c({5, 0}, 5);
    auto id = d.add(c);
    std::array cutters{line({5, -8}, {5, 8})};
    edit::trim(d, id, cutters, {10, 0}).apply(d);
    auto& arc = get<Arc>(id).curve();
    near(arc.pointAt(0), {5, 5});
    near(arc.pointAt(1), {5, -5});
    EXPECT_NEAR(arc.sweep(), pi, EPSILON);
    indexed(id, {10, 0}, false);
    indexed(id, {0, 0}, true);
    ASSERT_TRUE(d.undo());
    EXPECT_NE(dynamic_cast<const Circle*>(d.entity(id)), nullptr);
    indexed(id, {10, 0}, true);
    auto tangent = line({-1, 5}, {11, 5});
    EXPECT_THROW(edit::trim(d, id, std::array{tangent}, {0, 0}), std::invalid_argument);
}
TEST_F(Edit, TrimClockwiseArcFiltersAgainstCutterSweep) {
    auto a = d.add(Arc(CircularArc({}, 5, pi, -pi)));
    std::array cutter{d.add(Arc(CircularArc({0, 8}, 3, -pi, pi)))};
    edit::trim(d, a, cutter, {-5, 0}).apply(d);
    auto& arc = get<Arc>(a).curve();
    near(arc.pointAt(0), {0, 5});
    near(arc.pointAt(1), {5, 0});
    EXPECT_NEAR(arc.sweep(), -pi / 2, EPSILON);
    indexed(a, {-3, 0}, false);
    ASSERT_TRUE(d.undo());
    auto missing = d.add(Arc(CircularArc({0, 8}, 3, 0, pi)));
    EXPECT_THROW(edit::trim(d, a, std::array{missing}, {-5, 0}), std::invalid_argument);
}
TEST_F(Edit, TrimClosedPolylineOpensAtTwoCutsAndSplitsClosingBulge) {
    auto p = d.add(Polyline({{{0, 0}, 0}, {{10, 0}, 0}, {{10, 10}, 0}, {{0, 10}, 1}}, true));
    auto blade = line({-10, 5}, {15, 5});
    edit::trim(d, p, std::array{blade}, {5, 0}).apply(d);
    auto& poly = get<Polyline>(p);
    auto v = poly.vertices();
    EXPECT_FALSE(poly.closed());
    ASSERT_EQ(v.size(), 4u);
    near(v.front().point, {10, 5});
    near(v.back().point, {-5, 5});
    EXPECT_NEAR(v[2].bulge, std::tan(pi / 8), EPSILON);
    EXPECT_DOUBLE_EQ(v.back().bulge, 0);
    indexed(p, {2, 0}, false);
}
TEST_F(Edit, ExtendNearestForwardIntersectionClipsBoundariesAndNeverShrinks) {
    auto a = line({1, 1}, {4, 1});
    std::array bounds{line({6, 4}, {6, 8}), d.add(Circle({10, 1}, 2)), line({-3, -2}, {-3, 5})};
    edit::extend(d, a, bounds, {4, 1}).apply(d);
    near(get<Line>(a).end(), {8, 1});
    indexed(a, {7, 1}, true);
    edit::extend(d, a, bounds, {1, 1}).apply(d);
    near(get<Line>(a).start(), {-3, 1});
    auto rev = d.revision();
    EXPECT_THROW(edit::extend(d, a, std::array{bounds[0]}, {8, 1}), std::invalid_argument);
    EXPECT_EQ(d.revision(), rev);
}
TEST_F(Edit, ExtendClockwiseArcAtEitherEndToNextBoundary) {
    auto a = d.add(Arc(CircularArc({}, 5, pi / 2, -pi / 2)));
    auto cut = line({-8, 0}, {-2, 0});
    edit::extend(d, a, std::array{cut}, {0, 5}).apply(d);
    auto& curve = get<Arc>(a).curve();
    near(curve.pointAt(0), {-5, 0});
    near(curve.pointAt(1), {5, 0});
    EXPECT_NEAR(curve.sweep(), -pi, EPSILON);
    auto cut2 = line({0, -8}, {0, -2});
    edit::extend(d, a, std::array{cut2}, {5, 0}).apply(d);
    near(get<Arc>(a).curve().pointAt(1), {0, -5});
    EXPECT_NEAR(get<Arc>(a).curve().sweep(), -1.5 * pi, EPSILON);
}
TEST_F(Edit, OffsetUsesPickedSideAndRejectsCollapsedRadius) {
    auto a = line({1, 2}, {4, 2});
    edit::offset(d, a, 2, {1, -9}).apply(d);
    near(get<Line>(2).end(), {4, 0});
    near(get<Line>(a).end(), {4, 2});
    auto c = d.add(Circle({3, 5}, 4));
    edit::offset(d, c, 1, {3, 5}).apply(d);
    EXPECT_DOUBLE_EQ(get<Circle>(4).radius(), 3);
    auto rev = d.revision();
    for (auto n : {4., 5., 4. - EPSILON / 2})
        EXPECT_THROW(edit::offset(d, c, n, {3, 5}), std::invalid_argument);
    EXPECT_EQ(d.revision(), rev);
    auto arc = d.add(Arc(CircularArc({3, 5}, 4, .2, -pi)));
    edit::offset(d, arc, 1, {13, 5}).apply(d);
    auto& out = get<Arc>(d.entityIds().back()).curve();
    EXPECT_NEAR(out.sweep(), -pi, EPSILON);
    EXPECT_DOUBLE_EQ(out.radius(), 5);
}
TEST_F(Edit, OffsetMixedPolylineJoinsTangentArcAndMiterIncludingClosedSeam) {
    auto p = d.add(Polyline({{{0, 0}, 0}, {{10, 0}, 1}, {{10, 10}, 0}, {{0, 10}, 0}}));
    edit::offset(d, p, 2, {5, 1}).apply(d);
    auto v = get<Polyline>(2).vertices();
    ASSERT_EQ(v.size(), 4u);
    near(v[1].point, {10, 2});
    near(v[2].point, {10, 8});
    EXPECT_NEAR(v[1].bulge, 1, EPSILON);
    near(v[3].point, {0, 8});
    auto sq = d.add(Polyline({{{0, 0}, 0}, {{10, 0}, 0}, {{10, 10}, 0}, {{0, 10}, 0}}, true));
    edit::offset(d, sq, 1, {5, 1}).apply(d);
    auto& ring = get<Polyline>(4);
    EXPECT_TRUE(ring.closed());
    near(ring.vertices()[0].point, {1, 1});
    near(ring.vertices()[2].point, {9, 9});
    EXPECT_THROW(edit::offset(d, sq, 6, {5, 1}), std::invalid_argument);
}
TEST_F(Edit, DisjointOuterOffsetSupportsUseAnalyticRoundJoinAndCrossingOutputIsRejected) {
    auto id = d.add(Polyline({{{-5, 0}, 0}, {{0, 0}, -std::tan(pi / 8)}, {{1, 1}, 0}}));
    edit::offset(d, id, .75, {-3, -2}).apply(d);
    auto v = get<Polyline>(2).vertices();
    ASSERT_EQ(v.size(), 4u);
    near(v[1].point, {0, -.75});
    near(v[2].point, {.75, 0});
    auto join = CircularArc::fromBulge(v[1].point, v[2].point, v[1].bulge);
    near(join.center(), {});
    EXPECT_NEAR(join.radius(), .75, EPSILON);
    EXPECT_NEAR(join.sweep(), pi / 2, EPSILON);
    auto bow = d.add(Polyline({{{0, 0}, 0}, {{8, 8}, 0}, {{0, 8}, 0}, {{8, 0}, 0}}));
    EXPECT_THROW(edit::offset(d, bow, .1, {1, 0}), std::invalid_argument);
}
TEST_F(Edit, FilletKeepsPickedRaysAtZeroRadiusAndIsTangentAtBothEnds) {
    auto a = line({12, 0}, {-5, 0}), b = line({0, -1}, {0, 10});
    edit::fillet(d, a, {8, .01}, b, {0, 7}, 0).apply(d);
    near(get<Line>(a).end(), {});
    near(get<Line>(b).start(), {});
    EXPECT_EQ(d.size(), 2u);
    ASSERT_TRUE(d.undo());
    edit::fillet(d, a, {8, 0}, b, {0, 7}, 3).apply(d);
    auto& arc = get<Arc>(3).curve();
    auto t1 = get<Line>(a).end(), t2 = get<Line>(b).start();
    near(t1, arc.pointAt(0));
    near(t2, arc.pointAt(1));
    near(arc.center(), {3, 3});
    EXPECT_NEAR(arc.sweep(), -pi / 2, EPSILON);
    EXPECT_NEAR((t1 - arc.center()).dot((get<Line>(a).start() - t1).normalized()), 0, EPSILON);
    EXPECT_NEAR((t2 - arc.center()).dot((get<Line>(b).end() - t2).normalized()), 0, EPSILON);
    EXPECT_TRUE(d.undo());
    EXPECT_TRUE(d.canRedo());
    EXPECT_THROW(edit::fillet(d, a, {8, 0}, b, {0, 7}, 20), std::invalid_argument);
    EXPECT_TRUE(d.canRedo());
}
TEST_F(Edit, ObliqueFilletTouchesBothFiniteSegmentsForEachChoiceOfRetainedRays) {
    Vec2 corner{23, -7}, u{.6, .8}, v{.6, -.8};
    auto a = line(corner - u * 20, corner + u * 24), b = line(corner + v * 19, corner - v * 17);
    constexpr double radius = 1.75;
    for (double signA : {-1., 1.})
        for (double signB : {-1., 1.}) {
            edit::fillet(d, a, corner + u * (signA * 7), b, corner + v * (signB * 6), radius).apply(d);
            auto& arc = get<Arc>(d.entityIds().back()).curve();
            auto p = signA > 0 ? get<Line>(a).start() : get<Line>(a).end();
            auto q = signB > 0 ? get<Line>(b).end() : get<Line>(b).start();
            near(arc.pointAt(0), p);
            near(arc.pointAt(1), q);
            EXPECT_NEAR(arc.radius(), radius, EPSILON);
            EXPECT_NEAR((p - arc.center()).dot(u), 0, EPSILON);
            EXPECT_NEAR((q - arc.center()).dot(v), 0, EPSILON);
            ASSERT_TRUE(d.undo());
        }
}
TEST_F(Edit, GripLocationsFollowDisplayedIndicesAndPreserveBulges) {
    auto l = line({0, 0}, {2, 0});
    edit::grip(d, l, 2, {4, 5}).apply(d);
    near(get<Line>(l).start(), {3, 5});
    edit::grip(d, l, 1, {9, 8}).apply(d);
    near(get<Line>(l).end(), {9, 8});
    auto c = d.add(Circle({}, 5));
    edit::grip(d, c, 0, {2, 2}).apply(d);
    edit::grip(d, c, 3, {2, -1}).apply(d);
    EXPECT_DOUBLE_EQ(get<Circle>(c).radius(), 3);
    near(get<Circle>(c).center(), {2, 2});
    auto a = d.add(Arc(CircularArc({}, 5, 0, -pi)));
    edit::grip(d, a, 1, {-7, 2}).apply(d);
    auto& arc = get<Arc>(a).curve();
    near(arc.pointAt(0), {5, 0});
    near(arc.pointAt(1), {-7, 2});
    near(arc.closestPoint({0, -5}), {0, -5});
    EXPECT_LT(arc.sweep(), 0);
    auto p = d.add(Polyline({{{1, 0}, .4}, {{5, 3}, 0}, {{8, 4}, 0}}));
    edit::grip(d, p, 2, {7, 4}).apply(d);
    near(get<Polyline>(p).vertices()[1].point, {7, 4});
    EXPECT_DOUBLE_EQ(get<Polyline>(p).vertices()[0].bulge, .4);
    EXPECT_THROW(edit::grip(d, p, 4, {7, 4}), std::invalid_argument);
}
TEST_F(Edit, LockedOrStalePlansCannotPartiallyPublishOrDropRedo) {
    Layer layer("lock");
    auto l = line({1, 0}, {2, 0});
    EntityId locked = 0;
    d.transact([&](Transaction& tx) {
        auto id = tx.addLayer(layer);
        Circle c({}, 1);
        c.traits.layer = id;
        locked = tx.add(c);
        layer.isLocked = true;
        tx.updateLayer(id, layer);
    });
    auto plan = edit::move(d, std::array{l}, {0, 8});
    d.add(Circle({}, 2));
    d.undo();
    auto rev = d.revision();
    EXPECT_THROW(plan.apply(d), std::invalid_argument);
    EXPECT_THROW(edit::erase(d, std::array{l, locked}), std::invalid_argument);
    EXPECT_TRUE(d.canRedo());
    EXPECT_EQ(d.revision(), rev);
    near(get<Line>(l).start(), {1, 0});
}
TEST(Tracking, OrthoBothAxesAndPolarCaptureAreRelativeToBase) {
    using edit::Tracking;
    Vec2 base{1e7, -1e7};
    near(edit::constrain(base, base + Vec2{-6, 3}, Tracking::Ortho, 0).point, base + Vec2{-6, 0});
    near(edit::constrain(base, base + Vec2{1, -4}, Tracking::Ortho, 0).point, base + Vec2{0, -4});
    for (int degrees : {15, 30, 45}) {
        double a = -degrees * pi / 180;
        Vec2 ray{std::cos(a), std::sin(a)};
        auto r = edit::constrain(base, base + ray * 100 + ray.leftNormal(), Tracking::Polar, -a);
        EXPECT_TRUE(r.guided);
        near(r.point, base + ray * 100);
    }
    auto p = base + Vec2{10, 2};
    auto free = edit::constrain(base, p, Tracking::Polar, pi / 4);
    EXPECT_FALSE(free.guided);
    EXPECT_EQ(free.point, p);
}
} // namespace
