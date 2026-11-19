// SPDX-License-Identifier: LGPL-2.1-or-later
#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <planedraft/drafting.hpp>
namespace {
using namespace pd;
constexpr double pi = std::numbers::pi;
Polyline square(double a, double b) {
    return Polyline({{{a, a}, 0}, {{b, a}, 0}, {{b, b}, 0}, {{a, b}, 0}}, true);
}
void near(Vec2 a, Vec2 b) {
    EXPECT_NEAR(a.x, b.x, EPSILON);
    EXPECT_NEAR(a.y, b.y, EPSILON);
}
TEST(Drafting, DimensionsMeasureDefPointsNotTheTextOrOffset) {
    std::vector<Vec2> p{{2, 3}, {5, 7}, {1, 14}};
    DimStyle s;
    s.precision = 3;
    s.suffix = " mm";
    Dimension aligned(DimKind::Aligned, p, s);
    EXPECT_EQ(aligned.label(), "5.000 mm");
    EXPECT_NEAR(Dimension(DimKind::Linear, p).measurement(), 3, EPSILON);
    EXPECT_NEAR(Dimension(DimKind::Linear, p, {}, pi / 2).measurement(), 4, EPSILON);
    EXPECT_EQ(Dimension(DimKind::Radius, p).label(), "R5.00");
    EXPECT_EQ(Dimension(DimKind::Diameter, p).label(), "Ø10.00");
    Dimension angle(DimKind::Angular, {{2, 3}, {4, 3}, {2, 5}, {1, 2}});
    EXPECT_EQ(angle.label(), "270.00°");
    auto corners = [&](const Dimension& d) {
        return std::count_if(d.primitives().begin(), d.primitives().end(),
                             [](auto& e) { return dynamic_cast<const SolidFace*>(e.get()); });
    };
    EXPECT_EQ(corners(aligned), 2);
    s.arrow = DimStyle::Arrow::Tick;
    EXPECT_EQ(corners(Dimension(DimKind::Aligned, p, s)), 0);
    angle.transform(Matrix3x3::reflection({}, {1, 0}));
    EXPECT_NEAR(angle.measurement(), 270, EPSILON);
}
TEST(Drafting, DimensionGripRebuildsGeometryAndExplodeUndoRestoresEditableObject) {
    Document d;
    auto id = d.add(Dimension(DimKind::Aligned, {{0, 0}, {4, 3}, {0, 5}}));
    auto rev = d.revision();
    auto plan = edit::grip(d, id, 1, {6, 8});
    auto& ghost = dynamic_cast<const Dimension&>(*plan.items()[0].after);
    EXPECT_EQ(ghost.label(), "10.00");
    EXPECT_EQ(d.revision(), rev);
    plan.apply(d);
    auto& dim = dynamic_cast<const Dimension&>(*d.entity(id));
    EXPECT_EQ(dim.getGripPoints().at(1), (Vec2{6, 8}));
    auto count = dim.primitives().size();
    explode(d, std::array{id}).apply(d);
    EXPECT_FALSE(d.entity(id));
    EXPECT_EQ(d.size(), count);
    EXPECT_EQ(d.revision(), rev + 2);
    ASSERT_TRUE(d.undo());
    EXPECT_EQ(dynamic_cast<const Dimension&>(*d.entity(id)).measurement(), 10);
    ASSERT_TRUE(d.undo());
    EXPECT_EQ(dynamic_cast<const Dimension&>(*d.entity(id)).measurement(), 5);
}
TEST(Drafting, PatternLinesStopAtActualCircularIslandsForAllAngles) {
    auto outer = square(-10, 10);
    auto hole = Hatch::boundary(Circle({1, 2}, 4));
    for (auto type : {HatchPattern::ANSI31, HatchPattern::Grid, HatchPattern::Concrete})
        for (double angle : {0., -.25 * pi, .357}) {
            Hatch hatch({outer, hole}, type, .7, angle);
            ASSERT_GT(hatch.primitives().size(), 8u);
            for (auto& p : hatch.primitives()) {
                auto& l = dynamic_cast<const Line&>(*p);
                EXPECT_GE((closestPointOnSegment({1, 2}, l.start(), l.end()) - Vec2{1, 2}).length(),
                          4 - EPSILON);
                for (auto v : {l.start(), l.end()}) {
                    EXPECT_LE(std::abs(v.x), 10 + EPSILON);
                    EXPECT_LE(std::abs(v.y), 10 + EPSILON);
                }
            }
        }
}
TEST(Drafting, SolidTriangulatesConcaveRegionAndNestedIslandByParity) {
    Polyline outer({{{0, 0}, 0}, {{12, 0}, 0}, {{12, 6}, 0}, {{8, 6}, 0}, {{8, 12}, 0}, {{0, 12}, 0}}, true);
    Hatch h({outer, square(1, 5), square(2, 3)}, HatchPattern::Solid);
    double area = 0;
    for (auto& p : h.primitives()) {
        auto v = dynamic_cast<const SolidFace&>(*p).vertices();
        area += std::abs((v[1] - v[0]).cross(v[2] - v[0])) * .5;
        auto c = (v[0] + v[1] + v[2]) / 3;
        bool hole = c.x > 1 && c.y > 1 && c.x < 5 && c.y < 5;
        bool island = c.x >= 2 && c.x <= 3 && c.y >= 2 && c.y <= 3;
        EXPECT_FALSE(hole && !island);
    }
    EXPECT_NEAR(area, 144 - 24 - 16 + 1, EPSILON);
    EXPECT_THROW(Hatch({Polyline({{{0, 0}, 0}, {{1, 1}, 0}})}), std::invalid_argument);
    EXPECT_THROW(Hatch({square(-10, 10)}, HatchPattern::Grid, 2e-9), std::length_error);
}
TEST(Drafting, BlockGraphValidatesForwardReferencesBeforePublication) {
    BlockDependencies g{{"A", {"B"}}, {"B", {"C"}}, {"C", {}}};
    EXPECT_EQ(blockOrder(g), (std::vector<std::string>{"C", "B", "A"}));
    g["C"] = {"A"};
    EXPECT_THROW(blockOrder(g), CircularReferenceError);
    Document d;
    Line l({0, 0}, {5, 0});
    const Entity* members[]{&l};
    std::shared_ptr<const BlockDefinition> def;
    d.transact([&](auto& t) { def = t.defineBlock("A", members); });
    BlockReference self(def, {});
    members[0] = &self;
    auto rev = d.revision();
    EXPECT_THROW(d.transact([&](auto& t) { t.defineBlock("A", members); }), CircularReferenceError);
    EXPECT_EQ(d.revision(), rev);
}
TEST(Drafting, NestedInsertPlacementAndByBlockPropertiesSurviveAtomicExplode) {
    Document d;
    EntityId id;
    LayerId layer;
    d.transact([&](auto& t) {
        Layer l("parent");
        l.lineWeight = .7;
        l.color = {5, 15, 25};
        layer = t.addLayer(l);
        Line part({1, 0}, {3, 0});
        part.traits.color = ByBlock{};
        part.traits.lineWeight = ByLayer{};
        const Entity* member = &part;
        auto inner = t.defineBlock("pin", {&member, 1});
        BlockReference ref(inner, {3, 1}, {2, 2}, pi / 2);
        ref.traits.color = ByBlock{};
        member = &ref;
        auto outer = t.defineBlock("mount", {&member, 1});
        BlockReference top(outer, {100, 20}, {2, 1});
        top.traits.layer = layer;
        top.traits.color = Color{90, 80, 70};
        id = t.add(top);
    });
    auto rev = d.revision();
    auto plan = explode(d, std::array{id});
    ASSERT_EQ(d.size(), 1u);
    plan.apply(d);
    ASSERT_EQ(d.revision(), rev + 1);
    auto& out = dynamic_cast<const Line&>(*d.entity(d.entityIds().front()));
    near(out.start(), {106, 23});
    near(out.end(), {106, 27});
    EXPECT_EQ(out.traits.layer, layer);
    EXPECT_EQ(d.resolveStyle(out).color, (Color{90, 80, 70}));
    EXPECT_DOUBLE_EQ(d.resolveStyle(out).lineWeight, .7);
    d.undo();
    ASSERT_TRUE(dynamic_cast<const BlockReference*>(d.entity(id)));
    EXPECT_EQ(d.query({{106, 24}, {106, 25}}), std::vector<EntityId>{id});
    d.redo();
    EXPECT_FALSE(d.entity(id));
}
TEST(Drafting, MemberTreeCullsInPlacedCoordinatesButIncludesExternalArcCentres) {
    Document doc;
    std::shared_ptr<const BlockDefinition> def;
    Line row({70, 20}, {71, 21});
    Arc distant(CircularArc({30, 0}, 20, 0, .1));
    const Entity* parts[]{&row, &distant};
    doc.transact([&](auto& t) { def = t.defineBlock("symbols", parts); });
    auto place = Matrix3x3::translation({1e7, -1e7}) * Matrix3x3::rotation(.6) * Matrix3x3::scaling(3, 1);
    auto q = place.applyPoint({30, 0});
    BoundingBox2D aperture(q - Vec2{.1, .1}, q + Vec2{.1, .1});
    int n = 0;
    def->visitMembers(place, &aperture, [&](const Entity& e) {
        EXPECT_TRUE(dynamic_cast<const Arc*>(&e));
        ++n;
    });
    EXPECT_EQ(n, 1);
    BlockReference ref(def, {});
    ref.transform(place);
    auto id = doc.add(ref);
    EXPECT_EQ(doc.queryExtraPoints(aperture), std::vector<EntityId>{id});
    int centres = 0;
    visitInsertFeatures(
        doc, ref, SnapType::Center,
        [&](auto v) {
            near(q, v);
            ++centres;
        },
        &aperture);
    EXPECT_EQ(centres, 1);
}
TEST(Drafting, ExplodingClosingBulgeRetainsArcDirection) {
    Document d;
    auto id = d.add(Polyline({{{1, 0}, 0}, {{1, 4}, -.75}}, true));
    explode(d, std::array{id}).apply(d);
    auto ids = d.entityIds();
    ASSERT_EQ(ids.size(), 2u);
    auto& a = dynamic_cast<const Arc&>(*d.entity(ids[1])).curve();
    near(a.pointAt(0), {1, 4});
    near(a.pointAt(1), {1, 0});
    EXPECT_LT(a.sweep(), 0);
    ASSERT_TRUE(d.undo());
    ASSERT_TRUE(dynamic_cast<const Polyline*>(d.entity(id)));
}
TEST(Drafting, PaperSpaceUsesSeparateBoundsAndImmutableUndoableSettings) {
    Document d;
    auto id = d.add(Line({1e7, 2e7}, {1e7 + 10, 2e7}));
    auto bounds = d.extents();
    auto l = Layout::standard("Plan", Paper::A4, true, {1e7, 2e7}, .5);
    auto text = std::make_shared<Text>("AB", BoundingBox2D({0, 0}, {5, 2}), Matrix3x3{});
    l.paperEntities.push_back(text);
    EntityId sheet;
    d.transact([&](auto& t) { sheet = t.addLayout(l); });
    text->transform(Matrix3x3::translation({25, 10}));
    auto* stored = d.layout(sheet);
    near(stored->paperEntities[0]->getBounds().min(), {});
    EXPECT_EQ(d.extents(), bounds);
    auto vp = l.viewports[0];
    auto mid = midpoint(vp.getBounds().min(), vp.getBounds().max());
    near(vp.modelToPaper().applyPoint({1e7 + 10, 2e7}), mid + Vec2{5, 0});
    EXPECT_EQ(d.query(vp.modelWindow()), std::vector<EntityId>{id});
    l.landscape = false;
    l.viewports.clear();
    d.transact([&](auto& t) { t.updateLayout(sheet, l); });
    near(d.layout(sheet)->size(), {210, 297});
    d.undo();
    near(d.layout(sheet)->size(), {297, 210});
    d.undo();
    EXPECT_FALSE(d.layout(sheet));
    EXPECT_TRUE(d.entity(id));
}
} // namespace
