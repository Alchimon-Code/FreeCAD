// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/entities/primitives.hpp>

#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <stdexcept>

namespace {
using namespace pd;
void expectVecNear(Vec2 actual, Vec2 expected) {
    EXPECT_NEAR(actual.x, expected.x, EPSILON);
    EXPECT_NEAR(actual.y, expected.y, EPSILON);
}

TEST(Math, VectorAffineCompositionAndClosedBounds) {
    Vec2 v{3, 4};
    EXPECT_DOUBLE_EQ(v.length(), 5);
    expectVecNear(v.normalized(), {0.6, 0.8});
    EXPECT_DOUBLE_EQ(v.dot({1, 2}), 11);
    EXPECT_DOUBLE_EQ(v.cross({1, 2}), 2);
    EXPECT_THROW((void)Vec2{}.normalized(), std::invalid_argument);
    auto m = Matrix3x3::translation({10, -3}) * Matrix3x3::rotation(std::numbers::pi / 2) *
             Matrix3x3::scaling(2, 3);
    expectVecNear(m.applyPoint({1, 2}), {4, -1});
    expectVecNear(m.applyVector({1, 2}), {-6, 2});
    expectVecNear(Matrix3x3::reflection({2, 1}, {1, 1}).applyPoint({2, 3}), {4, 1});
    BoundingBox2D box;
    EXPECT_TRUE(box.empty());
    box.include({-3, 1});
    box.include({4, 2});
    EXPECT_TRUE(box.intersects({{4, 2}, {5, 3}}));
    EXPECT_FALSE(box.intersects({}));
    EXPECT_FALSE(box.contains(Vec2{5, 0}));
    EXPECT_TRUE(box.contains(box));
    auto transformed = m.applyBounds({{0, 0}, {1, 2}});
    expectVecNear(transformed.min(), {4, -3});
    expectVecNear(transformed.max(), {10, -1});
    EXPECT_THROW(BoundingBox2D({2, 1}, {1, 2}), std::invalid_argument);
}

TEST(Math, SegmentIntersectionsAndClosestPoint) {
    using K = Intersection::Kind;
    auto x = intersectSegments({0, 0}, {4, 2}, {0, 2}, {4, 0});
    EXPECT_EQ(x.kind, K::OnePoint);
    expectVecNear(x.first, {2, 1});
    x = intersectSegments({3, 0}, {1, 0}, {2, 0}, {4, 0});
    EXPECT_EQ(x.kind, K::Overlap);
    expectVecNear(x.first, {3, 0});
    expectVecNear(x.second, {2, 0});
    EXPECT_EQ(intersectSegments({0, 0}, {1, 0}, {1, 0}, {1, 2}).kind, K::OnePoint);
    EXPECT_EQ(intersectSegments({0, 0}, {2, 0}, {1, 0}, {1, 0}).kind, K::OnePoint);
    EXPECT_EQ(intersectSegments({0, 0}, {1, 0}, {2, 0}, {3, 0}).kind, K::None);
    EXPECT_EQ(intersectSegments({0, 0}, {1, 0}, {0, 1}, {1, 1}).kind, K::None);
    expectVecNear(closestPointOnSegment({1, 4}, {0, 0}, {2, 0}), {1, 0});
    expectVecNear(closestPointOnSegment({4, 4}, {0, 0}, {2, 0}), {2, 0});
}

TEST(Math, CircleIntersectionIsClippedToSegment) {
    using K = Intersection::Kind;
    auto x = intersectSegmentCircle({-3, 0}, {3, 0}, {0, 0}, 2);
    EXPECT_EQ(x.kind, K::TwoPoints);
    expectVecNear(x.first, {-2, 0});
    expectVecNear(x.second, {2, 0});
    EXPECT_EQ(intersectSegmentCircle({1, 0}, {3, 0}, {0, 0}, 2).kind, K::OnePoint);
    EXPECT_EQ(intersectSegmentCircle({3, 0}, {5, 0}, {0, 0}, 2).kind, K::None);
    x = intersectSegmentCircle({-3, 2}, {3, 2}, {0, 0}, 2);
    EXPECT_EQ(x.kind, K::OnePoint);
    expectVecNear(x.first, {0, 2});
}

TEST(Math, CircleIntersectionsAndArcProjection) {
    using K = Intersection::Kind;
    auto x = intersectCircles({0, 0}, 5, {8, 0}, 5);
    EXPECT_EQ(x.kind, K::TwoPoints);
    expectVecNear(x.first, {4, 3});
    expectVecNear(x.second, {4, -3});
    x = intersectCircles({0, 0}, 1, {2, 0}, 1);
    EXPECT_EQ(x.kind, K::OnePoint);
    expectVecNear(x.first, {1, 0});
    x = intersectCircles({0, 0}, 1, {2, 0}, 3);
    EXPECT_EQ(x.kind, K::OnePoint);
    expectVecNear(x.first, {-1, 0});
    EXPECT_EQ(intersectCircles({0, 0}, 1e9, {3e9, 0}, 2e9).kind, K::OnePoint);
    EXPECT_EQ(intersectCircles({0, 0}, 1, {0, 0}, 1).kind, K::Coincident);
    EXPECT_EQ(intersectCircles({0, 0}, 1, {0, 0}, 3).kind, K::None);
    EXPECT_EQ(intersectCircles({0, 0}, 1, {4, 0}, 1).kind, K::None);
    expectVecNear(closestPointOnCircle({0, 0}, {0, 0}, 2), {2, 0});
    CircularArc arc({0, 0}, 2, std::numbers::pi / 4, -std::numbers::pi / 2);
    expectVecNear(arc.closestPoint({10, 0}), {2, 0});
    expectVecNear(arc.closestPoint({0, 3}), arc.pointAt(0));
}

TEST(Entities, CircleAndArcKeepAnalyticGeometry) {
    Circle circle({3, 4}, 2);
    auto copy = circle.clone();
    auto m = Matrix3x3::translation({1, 0}) * Matrix3x3::scaling(-2, 2);
    circle.transform(m);
    expectVecNear(circle.center(), {-5, 8});
    EXPECT_DOUBLE_EQ(circle.radius(), 4);
    EXPECT_EQ(circle.getSnapPoints(SnapType::Quadrant).size(), 4u);
    EXPECT_EQ(circle.getGripPoints().size(), 5u);
    expectVecNear(copy->getBounds().min(), {1, 2});
    auto before = circle.getBounds();
    EXPECT_THROW(circle.transform(Matrix3x3(1, 0.5, 0, 0, 1, 0)), std::invalid_argument);
    EXPECT_EQ(circle.getBounds(), before);
    Arc arc(CircularArc({1, 2}, 3, 0, std::numbers::pi / 2));
    arc.transform(m);
    expectVecNear(arc.curve().pointAt(0), m.applyPoint({4, 2}));
    expectVecNear(arc.curve().pointAt(1), m.applyPoint({1, 5}));
    EXPECT_LT(arc.curve().sweep(), 0);
    EXPECT_THROW(Circle({}, -1), std::invalid_argument);
    EXPECT_THROW(Circle({}, std::numeric_limits<double>::infinity()), std::invalid_argument);
    EXPECT_THROW(Line({0, 0}, {EPSILON / 2, 0}), std::invalid_argument);
    EXPECT_THROW(Line({0, 0}, {std::numeric_limits<double>::quiet_NaN(), 2}), std::invalid_argument);
}

TEST(Entities, BulgeMajorArcExtremaMidpointAndReflection) {
    Polyline p({{{0, 0}, 2}, {{2, 0}, 0}});
    expectVecNear(p.getBounds().min(), {-0.25, -2});
    expectVecNear(p.getBounds().max(), {2.25, 0});
    expectVecNear(p.getSnapPoints(SnapType::Center).at(0), {1, -0.75});
    expectVecNear(p.getSnapPoints(SnapType::Midpoint).at(0), {1, -2});
    EXPECT_EQ(p.getSnapPoints(SnapType::Quadrant).size(), 3u);
    auto before = p.getBounds();
    EXPECT_THROW(p.transform(Matrix3x3::scaling(2, 1)), std::invalid_argument);
    EXPECT_EQ(p.getBounds(), before);
    p.transform(Matrix3x3::reflection({0, 0}, {1, 0}));
    EXPECT_DOUBLE_EQ(p.vertices()[0].bulge, -2);
    expectVecNear(p.getBounds().min(), {-0.25, 0});
    expectVecNear(p.getBounds().max(), {2.25, 2});
    expectVecNear(p.getSnapPoints(SnapType::Midpoint).at(0), {1, 2});
}

TEST(Entities, ClosingBulgeIsNotDiscarded) {
    Polyline p({{{0, 0}, 0}, {{2, 0}, 1}}, true);
    expectVecNear(p.getBounds().min(), {0, 0});
    expectVecNear(p.getBounds().max(), {2, 1});
    EXPECT_EQ(p.getGripPoints().size(), 4u);
    expectVecNear(p.getSnapPoints(SnapType::Midpoint).at(1), {1, 1});
    EXPECT_THROW(Polyline({{{0, 0}, 0}, {{2, 0}, 1}}, false), std::invalid_argument);
    Polyline straight({{{0, 0}, 0}, {{2, 1}, 0}});
    straight.transform(Matrix3x3::scaling(2, 1));
    expectVecNear(straight.vertices()[1].point, {4, 1});
    auto shallowArc = CircularArc::fromBulge({0, 0}, {1e10, 0}, 1e-10);
    expectVecNear(shallowArc.pointAt(0), {0, 0});
    expectVecNear(shallowArc.pointAt(1), {1e10, 0});
    auto mid = shallowArc.pointAt(0.5);
    EXPECT_DOUBLE_EQ(mid.x, 5e9);
    EXPECT_NEAR(mid.y, -0.5, EPSILON);
    EXPECT_NEAR(shallowArc.quadrantPoint(3).value().y, -0.5, EPSILON);
    Polyline shallow({{{0, 0}, 1e-10}, {{1e10, 0}, 0}});
    EXPECT_TRUE(shallow.getBounds().contains(Vec2{5e9, -0.5}));
    expectVecNear(shallow.getSnapPoints(SnapType::Midpoint).at(0), {5e9, -0.5});
}

TEST(Entities, TextMetricsAreInLocalCoordinates) {
    Text t("CAD", {{0, -1}, {4, 2}}, Matrix3x3::translation({10, 2}));
    auto copy = t.clone();
    t.transform(Matrix3x3::rotation(std::numbers::pi / 2));
    expectVecNear(t.getBounds().min(), {-4, 10});
    expectVecNear(t.getBounds().max(), {-1, 14});
    expectVecNear(t.getGripPoints().at(0), {-2, 10});
    expectVecNear(copy->getBounds().min(), {10, 1});
}
} // namespace
