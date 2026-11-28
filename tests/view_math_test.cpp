// SPDX-License-Identifier: LGPL-2.1-or-later
#include "dash.hpp"
#include "overlay.hpp"
#include "scene_cache.hpp"
#include "tessellation.hpp"
#include <planedraft/drafting.hpp>

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <utility>
#include <vector>

namespace {
using namespace pd;
constexpr double pi = std::numbers::pi;
Camera2D view(Vec2 eye = {}, double scale = 10) {
    Camera2D c;
    c.resize(1000, 600);
    c.setView(eye, scale);
    return c;
}
TEST(Camera2D, CursorAnchorRoundTripPanAndFloatProjectionAtSurveyCoordinates) {
    const Vec2 origin{1e7 + 0.125, -1e7 - 0.75};
    auto camera = view(origin, 38500.0);
    const Vec2 cursor{267.25, 143.875};
    const auto world = camera.screenToWorld(cursor);
    auto check = [&](Vec2 s) {
        auto actual = camera.worldToScreen(world);
        EXPECT_NEAR(actual.x, s.x, 0.002);
        EXPECT_NEAR(actual.y, s.y, 0.002);
        // Exactly the multiplications used by stroke.vert on float32 inputs. Casting before
        // subtracting would lose the entire feature at this scale.
        const auto v = camera.relativeToEye(world, origin);
        const auto m = camera.clipMatrix(origin);
        EXPECT_NEAR((m[0] * v[0] + m[6] + 1) * 500, s.x, 0.002);
        EXPECT_NEAR((1 - (m[4] * v[1] + m[7])) * 300, s.y, 0.002);
    };
    check(cursor);
    for (double factor : {1.017, 2.1, 0.83, 1e5, 2.0, 0.0625}) {
        camera.zoomAt(cursor, factor);
        check(cursor);
    }
    const Vec2 drag{42.75, -19.25};
    camera.pan(drag);
    check(cursor + drag);
    EXPECT_TRUE(camera.viewBounds().contains(world));
}
TEST(Camera2D, FitsCachedRootEvenForFlatAndEmptyDocuments) {
    Document doc;
    auto c = view();
    EXPECT_FALSE(c.fit(doc.extents()));
    auto id = doc.add(Line({1e7, 10}, {1e7 + 80, 10}));
    const auto initial = doc.extents();
    EXPECT_EQ(initial, doc.entity(id)->getBounds());
    ASSERT_TRUE(c.fit(initial));
    EXPECT_TRUE(c.viewBounds().contains(initial));
    EXPECT_EQ(c.eye(), midpoint(initial.min(), initial.max()));
    EXPECT_NEAR(c.worldToScreen(initial.min()).x, 50, 1e-7);
    doc.remove(id);
    EXPECT_TRUE(doc.extents().empty());
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(initial, doc.extents());
    auto saved = c.scale();
    ASSERT_TRUE(c.fit({{1, 2}, {1, 2}}));
    EXPECT_EQ(c.scale(), saved);
}
TEST(Tessellation, PhysicalPixelSagittaForCircleDirectedArcsAndClosingBulge) {
    std::vector<Stroke> curves{Ring{{1e7, -1e7}, 27}, CircularArc({1e7, -1e7}, 40, 1.3, -5.7)};
    Polyline poly({{{1e7 + 60, -1e7}, 0}, {{1e7 + 80, -1e7 + 5}, 0}, {{1e7 + 75, -1e7}, -1.6}}, true);
    visitStrokes(poly, [&](const Stroke& c) {
        if (std::holds_alternative<CircularArc>(c))
            curves.push_back(c);
    });
    ASSERT_EQ(curves.size(), 3u);
    for (const auto& curve : curves) {
        std::size_t prev = 0;
        auto clip = boundsOf(curve);
        const Vec2 pad{1, 1};
        clip = {clip.min() - pad, clip.max() + pad};
        for (double physicalScale : {0.001, 3.7, 18.2, 1000.0}) {
            std::vector<Segment> lines;
            tessellate(curve, lodScale(physicalScale), clip, [&](auto s) { lines.push_back(s); });
            ASSERT_GT(lines.size(), prev);
            prev = lines.size();
            if (physicalScale < 1) {
                EXPECT_LE(lines.size(), 4u);
            }
            for (auto chord : lines) {
                auto m = midpoint(chord.a, chord.b);
                EXPECT_LT((m - closestOn(curve, m)).length() * physicalScale, 0.5);
            }
            if (auto* arc = std::get_if<CircularArc>(&curve)) {
                EXPECT_EQ(lines.front().a, arc->pointAt(0));
                EXPECT_EQ(lines.back().b, arc->pointAt(1));
            } else {
                EXPECT_EQ(lines.front().a, lines.back().b);
            }
        }
    }
}
TEST(Tessellation, ClipsLocallyInsteadOfExpandingAnEntireHugeRing) {
    int count = 0;
    const BoundingBox2D clip({1e7 - 0.1, -0.1}, {1e7 + 0.1, 0.1});
    tessellate(Ring{{0, 0}, 1e7}, 1e6, clip, [&](Segment s) {
        EXPECT_TRUE(clip.contains(s.a));
        EXPECT_TRUE(clip.contains(s.b));
        ++count;
    });
    EXPECT_GT(count, 0);
    EXPECT_LT(count, 20);
}
TEST(SnapEngine, BoundedApertureFeaturePrecedenceAndAnalyticIntersection) {
    Document doc;
    auto a = doc.add(Line({-10, 0}, {10, 0}));
    auto b = doc.add(Line({3, -4}, {3, 10}));
    auto c = view();
    SnapEngine engine;
    auto find = [&](Vec2 p, SnapType mask = SnapEngine::modes) {
        return engine.find(doc, c, c.worldToScreen(p), 5, mask);
    };
    auto h = find({3.2, 0.1});
    ASSERT_TRUE(h);
    EXPECT_EQ(h->type, SnapType::Intersection);
    EXPECT_EQ(h->id, a);
    EXPECT_EQ(h->other, b);
    EXPECT_EQ(h->point, (Vec2{3, 0}));
    h = find({-9.8, 0.1});
    ASSERT_TRUE(h);
    EXPECT_EQ(h->type, SnapType::Endpoint);
    h = find({-0.1, 0.3});
    ASSERT_TRUE(h);
    EXPECT_EQ(h->type, SnapType::Midpoint);
    h = find({5, 0.2});
    ASSERT_TRUE(h);
    EXPECT_EQ(h->type, SnapType::Nearest);
    EXPECT_EQ(h->point, (Vec2{5, 0}));
    EXPECT_FALSE(find({-10.4, 0.4}, SnapType::Endpoint)); // inside square, outside circular aperture
    c.zoomAt(c.worldToScreen({5, 0}), 4);
    EXPECT_FALSE(find({5, 0.2})); // same world distance, now 8 px
}
TEST(SnapEngine, ArcAndBulgeCentersOutsideTightBoundsSurviveMoveUndoAndLayerVisibility) {
    Document doc;
    LayerId layer;
    EntityId id;
    doc.transact([&](Transaction& tx) {
        layer = tx.addLayer(Layer("arcs"));
        Arc arc({{0, 0}, 50, 0, 0.25});
        arc.traits.layer = layer;
        id = tx.add(arc);
    });
    SnapEngine engine;
    auto c = view();
    auto find = [&](Vec2 p) { return engine.find(doc, c, c.worldToScreen(p), 3, SnapType::Center); };
    EXPECT_TRUE(doc.query({{-1, -1}, {1, 1}}).empty());
    ASSERT_TRUE(find({}));
    EXPECT_EQ(find({})->id, id);
    doc.transform(id, Matrix3x3::translation({100, 100}));
    EXPECT_FALSE(find({}));
    EXPECT_TRUE(find({100, 100}));
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(find({}));
    doc.transact([&](Transaction& tx) {
        auto l = *doc.layer(layer);
        l.isFrozen = true;
        tx.updateLayer(layer, l);
    });
    EXPECT_FALSE(find({}));
    ASSERT_TRUE(doc.undo());
    doc.remove(id);
    EXPECT_FALSE(find({}));
    ASSERT_TRUE(doc.undo());
    EXPECT_TRUE(find({}));
    Polyline poly({{{10, 4}, 0.1}, {{17, 9}, 0}});
    auto pid = doc.add(poly);
    auto p = poly.getSnapPoints(SnapType::Center).front();
    ASSERT_FALSE(poly.getBounds().contains(p));
    ASSERT_TRUE(find(p));
    EXPECT_EQ(find(p)->id, pid);
}
TEST(SnapEngine, ExteriorInsertionDoesNotExpandTheDistantTextOutline) {
    Document doc;
    auto id = doc.add(Text("label", {{80, 50}, {110, 70}}, {}));
    auto c = view();
    SnapEngine engine(
        +[](const Text&, const StrokeSink&) { ADD_FAILURE() << "out-of-aperture text shaped"; });
    auto h = engine.find(doc, c, c.worldToScreen({}), 5, SnapEngine::modes | SnapType::Insertion);
    ASSERT_TRUE(h);
    EXPECT_EQ(h->id, id);
    EXPECT_EQ(h->type, SnapType::Insertion);
    EXPECT_FALSE(engine.find(doc, c, c.worldToScreen({}), 5));
}
TEST(SnapEngine, CircleArcIntersectionsFilterTheMissingSweepAndUseTrueBulgeNearest) {
    Document doc;
    auto c = view();
    SnapEngine engine;
    doc.add(Circle({0, 0}, 5));
    doc.add(Arc(CircularArc({6, 0}, 5, pi, -pi)));
    auto find = [&](Vec2 p, SnapType mode) { return engine.find(doc, c, c.worldToScreen(p), 3, mode); };
    ASSERT_TRUE(find({3, 4}, SnapType::Intersection));
    EXPECT_FALSE(find({3, -4}, SnapType::Intersection));
    Polyline poly({{{20, 0}, 0}, {{30, 0}, 1}}, true); // returning semicircle above the chord
    auto id = doc.add(poly);
    auto n = find({25, 5.2}, SnapType::Nearest);
    ASSERT_TRUE(n);
    EXPECT_EQ(n->id, id);
    EXPECT_NEAR(n->point.y, 5, 1e-10);
    auto m = find({25, 4.9}, SnapType::Midpoint);
    ASSERT_TRUE(m);
    EXPECT_EQ(m->point, (Vec2{25, 5}));
    doc.add(Line({25, 4}, {25, 8}));
    auto h = find({25, 4.9}, SnapType::Intersection);
    ASSERT_TRUE(h);
    EXPECT_NEAR(h->point.y, 5, 1e-10);
}
TEST(Selection, WindowContainsCrossingIntersectsStrokesNotBoundingBoxesOrDisks) {
    Document doc;
    auto c = view();
    SelectionManager pick;
    auto line = doc.add(Line({-10, -10}, {10, 10}));
    auto circle = doc.add(Circle({}, 5));
    auto poly = doc.add(Polyline({{{20, 0}, 0}, {{30, 0}, 1}}, true));
    auto frame = [&](Vec2 lo, Vec2 hi, bool crossing) {
        auto a = c.worldToScreen(lo), b = c.worldToScreen(hi);
        return crossing ? pick.frame(doc, c, b, a) : pick.frame(doc, c, a, b);
    };
    EXPECT_EQ(frame({-6, -6}, {6, 6}, false), (SelectionManager::Ids{circle}));
    EXPECT_EQ(frame({-1, -1}, {1, 1}, true), (SelectionManager::Ids{line})); // inside ring, no stroke
    EXPECT_TRUE(frame({-4, 8}, {-2, 9}, true).empty()); // inside diagonal AABB, not on segment
    EXPECT_EQ(frame({-1, 4.5}, {1, 5}, true), (SelectionManager::Ids{circle})); // tangency
    EXPECT_EQ(frame({24, 4}, {26, 6}, true), (SelectionManager::Ids{poly}));
    EXPECT_TRUE(frame({24, 4}, {26, 6}, false).empty());
    EXPECT_TRUE(frame({20, 4}, {21, 5}, true).empty()); // arc AABB corner
}
TEST(Picking, NestedBlockFeaturesAndCrossingUseLeafGeometryAndReportTopLevelHandle) {
    Document d;
    EntityId id;
    LayerId hidden;
    d.transact([&](auto& t) {
        Layer hl("hidden");
        hl.isVisible = false;
        hidden = t.addLayer(hl);
        Line part({2, 1}, {8, 1}), off({5, 2}, {6, 2});
        off.traits.layer = hidden;
        Circle circle({8, 8}, 2);
        const Entity* members[]{&part, &circle, &off};
        auto inner = t.defineBlock("leaf", members);
        BlockReference child(inner, {3, 1}, {2, 2});
        const Entity* r = &child;
        auto assembly = t.defineBlock("assembly", {&r, 1});
        id = t.add(BlockReference(assembly, {50, 20}, {1, 1}, pi / 2));
    });
    auto cam = view({50, 20}, 8);
    auto screen = [&](Vec2 p) { return cam.worldToScreen(p); };
    SnapEngine engine;
    auto hit = engine.find(d, cam, screen({47, 27}), 2, SnapType::Endpoint);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->id, id);
    EXPECT_EQ(hit->point, (Vec2{47, 27}));
    auto center = engine.find(d, cam, screen({33, 39}), 2, SnapType::Center);
    ASSERT_TRUE(center);
    EXPECT_EQ(center->id, id);
    EXPECT_FALSE(engine.find(d, cam, screen({45, 33}), 1, SnapType::Endpoint));
    SelectionManager sel;
    EXPECT_TRUE(sel.frame(d, cam, screen({44, 34}), screen({42, 32})).empty());
    EXPECT_EQ(sel.frame(d, cam, screen({48, 34}), screen({46, 32})), SelectionManager::Ids{id});
}
TEST(Picking, FilledAreaAndIslandHaveDifferentPickAndCrossingResults) {
    Document doc;
    auto box = [](double r) {
        return Polyline({{{-r, -r}, 0}, {{r, -r}, 0}, {{r, r}, 0}, {{-r, r}, 0}}, true);
    };
    auto id = doc.add(Hatch({box(30), box(4)}, HatchPattern::Solid));
    auto cam = view({}, 16);
    auto screen = [&](Vec2 p) { return cam.worldToScreen(p); };
    SnapEngine snap;
    SelectionManager sel;
    ASSERT_TRUE(snap.pick(doc, cam, screen({-18, -8}), 1));
    EXPECT_EQ(snap.pick(doc, cam, screen({-18, -8}), 1)->id, id);
    EXPECT_FALSE(snap.pick(doc, cam, screen({0, 1}), 1));
    EXPECT_EQ(sel.frame(doc, cam, screen({-18, -8}), screen({-20, -9})), SelectionManager::Ids{id});
    EXPECT_TRUE(sel.frame(doc, cam, screen({2, 2}), screen({-2, -2})).empty());
}
TEST(Overlay, DirectedFrameAndFeatureMarkersHaveDistinctScreenGeometry) {
    auto c = view();
    OverlayState state;
    state.frameStart = Vec2{100, 100};
    state.cursor = Vec2{170, 160};
    std::vector<RenderVertex> v;
    ASSERT_EQ(buildOverlay(v, c, state), 6u);
    const auto solid = v.size();
    EXPECT_GT(v.front().ink.b, v.front().ink.g);
    EXPECT_LT(v.front().ink.a, 255);
    std::swap(state.frameStart, state.cursor);
    ASSERT_EQ(buildOverlay(v, c, state), 6u);
    EXPECT_GT(v.front().ink.g, v.front().ink.b);
    EXPECT_GT(v.size(), solid); // spaced dash segments, not just a recolored solid outline
    state.frameStart.reset();
    buildOverlay(v, c, state);
    const auto baseline = v.size();
    for (auto [type, edges] : {std::pair{SnapType::Endpoint, 4},
                               {SnapType::Midpoint, 3},
                               {SnapType::Center, 24},
                               {SnapType::Intersection, 2},
                               {SnapType::Nearest, 4}}) {
        state.snap = SnapHit{c.screenToWorld(*state.cursor), type, 1, 0, 0};
        EXPECT_EQ(buildOverlay(v, c, state), 0u);
        EXPECT_EQ(v.size(), baseline + 2 * edges);
        EXPECT_GT(v.back().ink.g, v.back().ink.r);
    }
}
// A hidden or centre line is drawn as lengths of ink, and the pattern keeps its phase from one segment
// of an entity to the next: a dashed circle must read as one dashed circle.
TEST(Dash, HiddenLinesArriveInPiecesWithThePhaseCarriedOver) {
    auto ink = [](Dasher& dasher, std::initializer_list<Segment> segments) {
        double total = 0;
        std::vector<Segment> pieces;
        for (auto s : segments)
            dasher.split(s, [&](Segment piece) {
                pieces.push_back(piece);
                total += (piece.b - piece.a).length();
            });
        return std::pair{total, pieces};
    };
    Dasher whole("HIDDEN");
    ASSERT_FALSE(whole.solid());
    auto [oneLength, one] = ink(whole, {Segment{{0, 0}, {20, 0}}});
    ASSERT_EQ(one.size(), 3u); // dash, dash, and the dash the end of the line cuts short
    EXPECT_NEAR(one[0].a.x, 0., 1e-9);
    EXPECT_NEAR(one[0].b.x, 6., 1e-9);
    EXPECT_NEAR(one[1].a.x, 7.5, 1e-9);
    EXPECT_NEAR(one[2].b.x, 20., 1e-9);

    Dasher halves("HIDDEN");
    auto [twoLength, two] = ink(halves, {Segment{{0, 0}, {10, 0}}, Segment{{10, 0}, {20, 0}}});
    EXPECT_GT(two.size(), 0u);
    // Restarting the pattern at the joint would lay down more ink than the whole line carries.
    EXPECT_NEAR(twoLength, oneLength, 1e-9);
    EXPECT_NEAR(twoLength, 15., 1e-9);
}
TEST(Dash, ContinuousAndUnknownNamesAreDrawnWhole) {
    for (const char* name : {"Continuous", "ByLayer", "", "СПЛОШНАЯ", "MAKESHIFT"}) {
        Dasher dasher(name);
        EXPECT_TRUE(dasher.solid()) << name;
        int pieces = 0;
        dasher.split(Segment{{0, 0}, {5, 0}}, [&](Segment) { ++pieces; });
        EXPECT_EQ(pieces, 1) << name;
    }
}
TEST(Overlay, TheCrosshairIsBrightAndItsPickboxIsTwoPixelsThick) {
    auto cam = view();
    OverlayState state;
    state.cursor = Vec2{110, 60};
    std::vector<RenderVertex> v;
    ASSERT_EQ(buildOverlay(v, cam, state), 0u);
    // Four arms and two frames around the aperture, every one of them near white.
    EXPECT_EQ(v.size(), 2u * (4u + 8u));
    for (const auto& vertex : v) {
        EXPECT_GE(vertex.ink.r, 240);
        EXPECT_GE(vertex.ink.a, 230);
    }
}
TEST(Overlay, TrackingGuideIsClippedAndDashedInLogicalPixelsAndGhostIsTranslucent) {
    auto cam = view({}, 1);
    cam.resize(220, 100);
    OverlayState state;
    state.cursor = {170, 50};
    state.guideBase = Vec2{};
    state.effective = Vec2{60, 0};
    Document d;
    auto plan = edit::line(d, {-20, 5}, {60, 0});
    std::vector<RenderVertex> v;
    ASSERT_EQ(buildOverlay(v, cam, state, &plan), 0u);
    std::vector<Segment> dashes;
    bool ghost = false;
    for (size_t i = 0; i < v.size(); i += 2) {
        auto a = v[i], b = v[i + 1];
        if (a.ink.a == 200)
            dashes.push_back({{a.x, a.y}, {b.x, b.y}});
        if (a.ink.a == 168) {
            ghost = true;
            EXPECT_FLOAT_EQ(a.x, 90);
            EXPECT_FLOAT_EQ(a.y, 45);
            EXPECT_FLOAT_EQ(b.x, 170);
            EXPECT_FLOAT_EQ(b.y, 50);
        }
    }
    ASSERT_GT(dashes.size(), 10u);
    for (size_t i = 1; i < dashes.size(); ++i) {
        EXPECT_NEAR(dashes[i - 1].b.x - dashes[i - 1].a.x, 6, EPSILON);
        EXPECT_NEAR(dashes[i].a.x - dashes[i - 1].b.x, 4, EPSILON);
        EXPECT_DOUBLE_EQ(dashes[i].a.y, 50);
    }
    EXPECT_TRUE(ghost);
    EXPECT_EQ(d.size(), 0u);
    EXPECT_FALSE(d.canUndo());
}
TEST(SceneCache, OnlyVisibleMembershipDirtyOrLodChangesUploadAfterPanEditAndUndo) {
    Document doc;
    auto camera = view({}, 1);
    camera.resize(120, 80);
    LayerId layer;
    EntityId line, circle, hidden;
    doc.transact([&](Transaction& tx) {
        Layer style("cool");
        style.color = {14, 80, 200};
        layer = tx.addLayer(style);
        Line l({0, 0}, {12, 0});
        l.traits.layer = layer;
        line = tx.add(l);
        hidden = tx.add(Line({85, 0}, {86, 0})); // off screen but in SAME ID chunk
        for (int i = 0; i < 256; ++i) {
            tx.add(Line({1000., double(i)}, {1001., double(i)}));
        }
        circle = tx.add(Circle({3, 4}, 10));
    });
    SelectionManager selected;
    SceneCache cache;
    auto subscription = doc.observe([&](auto changes) {
        selected.reconcile(doc, changes);
        cache.invalidate(doc, changes, selected);
    });
    std::vector<EntityId> uploaded, evicted;
    struct Mesh {
        std::vector<RenderVertex> v;
        std::size_t lines;
    };
    std::map<EntityId, Mesh> gpu;
    auto sync = [&](double dpr = 1) {
        uploaded.clear();
        evicted.clear();
        cache.sync(
            doc, camera, dpr, selected,
            [&](EntityId key, Vec2, std::span<const RenderVertex> v, std::size_t n, std::size_t) {
                uploaded.push_back(key);
                gpu[key] = {std::vector<RenderVertex>(v.begin(), v.end()), n};
            },
            [&](EntityId key) {
                evicted.push_back(key);
                gpu.erase(key);
            });
    };
    const auto key = line / 256, ck = circle / 256;
    ASSERT_NE(key, ck);
    sync();
    ASSERT_EQ(gpu.size(), 2u);
    EXPECT_EQ(gpu[key].lines, 2u);
    EXPECT_EQ(gpu[key].v[0].ink.r, 14);
    sync();
    EXPECT_TRUE(uploaded.empty());
    camera.pan({5, 3});
    sync();
    EXPECT_TRUE(uploaded.empty());
    auto coarse = gpu[ck].lines;
    sync(2);
    EXPECT_EQ(uploaded, (std::vector<EntityId>{ck}));
    EXPECT_GT(gpu[ck].lines, coarse);
    doc.transform(hidden, Matrix3x3::translation({3, 0}));
    sync(2);
    EXPECT_TRUE(uploaded.empty());
    camera.pan({-40, 0});
    sync(2);
    EXPECT_EQ(uploaded, (std::vector<EntityId>{key}));
    EXPECT_EQ(gpu[key].lines, 4u);
    auto dirty = selected.replace({line});
    cache.invalidateSelection(dirty);
    sync(2);
    ASSERT_EQ(uploaded, (std::vector<EntityId>{key}));
    EXPECT_EQ(gpu[key].v[0].ink.r, selectionInk.r);
    EXPECT_EQ(gpu[key].v.size() - gpu[key].lines, 3u);
    doc.remove(line);
    sync(2);
    EXPECT_EQ(gpu[key].lines, 2u);
    EXPECT_TRUE(selected.ids().empty());
    ASSERT_TRUE(doc.undo());
    sync(2);
    EXPECT_EQ(gpu[key].lines, 4u);
    ASSERT_TRUE(doc.redo());
    sync(2);
    EXPECT_EQ(gpu[key].lines, 2u);
    doc.remove(circle);
    sync(2);
    EXPECT_EQ(evicted, (std::vector<EntityId>{ck}));
    EXPECT_FALSE(gpu.contains(ck));
}
TEST(SceneCache, ExteriorGripIsVisibleEvenWhenTheSelectedArcItselfIsCulled) {
    Document doc;
    Arc arc(CircularArc({}, 60, 0, 0.18));
    arc.traits.isSelected = true;
    auto id = doc.add(arc);
    auto camera = view({}, 100);
    auto area = camera.viewBounds();
    ASSERT_TRUE(doc.query(area).empty());
    SceneCache cache;
    SelectionManager pick;
    std::vector<RenderVertex> v;
    std::size_t lines = 0;
    auto sync = [&] {
        cache.sync(
            doc, camera, 1, pick,
            [&](auto, Vec2, auto data, auto n, auto) {
                v.assign(data.begin(), data.end());
                lines = n;
            },
            [&](auto) { v.clear(); });
    };
    sync();
    EXPECT_EQ(lines, 0u);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].ink.b, gripInk.b);
    auto changed = pick.replace({});
    cache.invalidateSelection(changed);
    sync();
    EXPECT_TRUE(v.empty());
    changed = pick.replace({id});
    cache.invalidateSelection(changed);
    sync();
    EXPECT_EQ(v.size(), 1u);
    EXPECT_EQ(doc.revision(), 1u);
}
TEST(SceneCache, VeryDeepZoomRebasesEvenAnUnmodifiedStraightChunk) {
    Document doc;
    const Vec2 base{1e7, -1e7};
    auto camera = view(base, 0.01);
    doc.add(Line(base - Vec2{1e6, 0}, base + Vec2{1e6, 0}));
    SceneCache cache;
    SelectionManager pick;
    int uploads = 0;
    Vec2 origin;
    auto sync = [&] {
        cache.sync(
            doc, camera, 1, pick,
            [&](auto, Vec2 eye, auto data, auto n, auto) {
                ++uploads;
                origin = eye;
                EXPECT_EQ(n, 2u);
                EXPECT_LT(std::abs(data.front().x) * camera.scale(), 1005.0);
            },
            [](auto) {});
    };
    sync();
    camera.setView(base + Vec2{10834.125, 0}, 1e5);
    sync();
    EXPECT_EQ(uploads, 2);
    EXPECT_EQ(origin, camera.eye());
}
TEST(SceneCache, LayerPacketRecolorsHidesAndRestoresOnlyAffectedVisibleChunks) {
    Document doc;
    LayerId layer;
    EntityId id;
    doc.transact([&](Transaction& tx) {
        layer = tx.addLayer(Layer("ink"));
        Line line({0, 0}, {1, 0});
        line.traits.layer = layer;
        id = tx.add(line);
    });
    SceneCache cache;
    SelectionManager pick;
    auto token = doc.observe([&](auto changes) {
        pick.reconcile(doc, changes);
        cache.invalidate(doc, changes, pick);
    });
    std::vector<RenderVertex> v;
    std::size_t uploads = 0, evictions = 0;
    auto sync = [&] {
        cache.sync(
            doc, view(), 1, pick,
            [&](auto, Vec2, auto data, auto, auto) {
                ++uploads;
                v.assign(data.begin(), data.end());
            },
            [&](auto) {
                ++evictions;
                v.clear();
            });
    };
    sync();
    doc.transact([&](Transaction& tx) {
        auto l = *doc.layer(layer);
        l.color = {20, 30, 40};
        tx.updateLayer(layer, l);
    });
    sync();
    ASSERT_EQ(uploads, 2u);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].ink.r, 20);
    pick.replace({id});
    doc.transact([&](Transaction& tx) {
        auto l = *doc.layer(layer);
        l.isVisible = false;
        tx.updateLayer(layer, l);
    });
    sync();
    EXPECT_EQ(evictions, 1u);
    EXPECT_TRUE(pick.ids().empty());
    ASSERT_TRUE(doc.undo());
    sync();
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].ink.g, 30);
}
} // namespace
