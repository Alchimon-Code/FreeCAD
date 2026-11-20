// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include "language.h"
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>
namespace {
using namespace pd;
using Step = EditController::Step;
void near(Vec2 a, Vec2 b) {
    EXPECT_NEAR(a.x, b.x, EPSILON);
    EXPECT_NEAR(a.y, b.y, EPSILON);
}
TEST(EditSession, EveryAffineToolCommitsExactlyItsPreviewOnEnter) {
    CommandSession s;
    s.execute("_LINE 1,0 3,0");
    s.setSelectionProvider([] { return std::vector<EntityId>{1}; });
    auto& d = s.document();
    auto& e = s.editor();
    struct Case {
        const char* cmd;
        Vec2 cursor, start, end;
    };
    for (auto c : {Case{"_MOVE", {2, 3}, {3, 3}, {5, 3}}, Case{"_COPY", {2, 3}, {3, 3}, {5, 3}},
                   Case{"_ROTATE", {0, 4}, {0, 1}, {0, 3}}, Case{"_SCALE", {4, 0}, {2, 0}, {6, 0}},
                   Case{"_MIRROR", {0, 10}, {-1, 0}, {-3, 0}}}) {
        SCOPED_TRACE(c.cmd);
        auto rev = d.revision();
        s.execute(c.cmd);
        ASSERT_EQ(e.step(), Step::Base);
        s.execute("0,0");
        if (e.step() == Step::Factor) {
            s.execute("R");
            ASSERT_EQ(e.step(), Step::Reference);
            s.execute("2");
        }
        for (int i = 0; i < 25; ++i)
            e.hover(Vec2{3., double(i)});
        e.hover(c.cursor);
        auto ghost = e.ghost();
        ASSERT_NE(ghost, nullptr);
        ASSERT_EQ(ghost->items().size(), 1u);
        auto line = dynamic_cast<const Line*>(ghost->items()[0].after.get());
        ASSERT_NE(line, nullptr);
        near(line->start(), c.start);
        near(line->end(), c.end);
        EXPECT_EQ(d.revision(), rev);
        EXPECT_EQ(d.size(), 1u);
        near(dynamic_cast<const Line&>(*d.entity(1)).start(), {1, 0});
        s.execute("");
        ASSERT_FALSE(e.active());
        ASSERT_EQ(d.revision(), rev + 1);
        auto& committed = dynamic_cast<const Line&>(*d.entity(d.entityIds().back()));
        near(committed.start(), c.start);
        near(committed.end(), c.end);
        s.execute("_UNDO");
        EXPECT_EQ(d.size(), 1u);
    }
    s.execute("_LINE");
    s.execute("7,8");
    e.hover(Vec2{-1, 4});
    ASSERT_NE(e.ghost(), nullptr);
    s.execute("");
    ASSERT_EQ(d.size(), 2u);
    near(dynamic_cast<const Line&>(*d.entity(d.entityIds().back())).end(), {-1, 4});
}
TEST(EditSession, TopologyUsesBoundarySetsAndPickedTargetNotSelectionOrder) {
    CommandSession s;
    s.execute("_LINE -10,0 10,0");
    s.execute("_LINE 0,-4 0,4");
    s.execute("_LINE 6,-4 6,4");
    auto& d = s.document();
    auto& e = s.editor();
    auto rev = d.revision();
    s.execute("_TRIM");
    ASSERT_EQ(e.step(), Step::Select);
    s.execute("#3 #2");
    ASSERT_EQ(e.step(), Step::Target);
    s.execute("#2 0,1");
    EXPECT_EQ(d.revision(), rev);
    s.execute("#1 2,0");
    ASSERT_FALSE(e.active());
    ASSERT_EQ(d.size(), 4u);
    near(dynamic_cast<const Line&>(*d.entity(1)).end(), {});
    std::vector<EntityId> selected{3};
    s.setSelectionProvider([&] { return selected; });
    s.execute("_EXTEND");
    s.execute("#1 -0.01,0");
    ASSERT_FALSE(e.active());
    near(dynamic_cast<const Line&>(*d.entity(1)).end(), {6, 0});
    selected = {1};
    s.execute("_OFFSET");
    s.execute("-2");
    ASSERT_EQ(e.step(), Step::Distance);
    s.execute("2");
    s.execute("0,5");
    ASSERT_EQ(d.size(), 5u);
    near(dynamic_cast<const Line&>(*d.entity(5)).end(), {6, 2});
    selected = {2, 1};
    s.execute("_FILLET");
    s.execute("2");
    s.execute("#1 -8,0");
    s.execute("#2 0,3");
    ASSERT_FALSE(e.active());
    ASSERT_EQ(d.size(), 6u);
    near(dynamic_cast<const Arc&>(*d.entity(6)).curve().center(), {-2, 2});
    s.execute("_UNDO");
    EXPECT_EQ(d.size(), 5u);
    EXPECT_EQ(d.revision(), rev + 5);
}
TEST(EditSession, RussianVocabularyAndStepPromptsSurviveLanguageSwitch) {
    CommandSession s;
    Language lang;
    ASSERT_TRUE(lang.setRussian(true));
    for (auto name : {"ПЕРЕНЕСТИ", "КОПИРОВАТЬ", "ПОВЕРНУТЬ", "МАСШТАБ", "ЗЕРКАЛО", "ОБРЕЗАТЬ", "УДЛИНИТЬ",
                      "СМЕСТИТЬ", "СОПРЯЖЕНИЕ", "СТЕРЕТЬ"})
        EXPECT_TRUE(s.completions().contains(name)) << name;
    s.execute("_LINE 2,0 6,0");
    EXPECT_TRUE(s.execute("повернуть").contains("Выберите"));
    s.execute("#1");
    EXPECT_TRUE(s.editor().prompt().contains("Базовая"));
    s.execute("0,0");
    ASSERT_TRUE(lang.setRussian(false));
    EXPECT_TRUE(s.editor().prompt().contains("degrees"));
    s.execute("90");
    near(dynamic_cast<const Line&>(*s.document().entity(1)).end(), {0, 6});
    ASSERT_TRUE(lang.setRussian(true));
    s.execute("стереть");
    s.execute("#1");
    EXPECT_EQ(s.document().size(), 0u);
    s.execute("отменить");
    EXPECT_EQ(s.document().size(), 1u);
    ASSERT_TRUE(lang.setRussian(false));
}
TEST(EditSession, TypedPointsUseTrackingAndNonuniformCircularScaleIsExplicitlyRefused) {
    CommandSession s;
    auto& e = s.editor();
    e.setTracking(edit::Tracking::Ortho);
    s.execute("_LINE 1,2 3,9");
    auto& d = s.document();
    near(dynamic_cast<const Line&>(*d.entity(1)).end(), {1, 9});
    s.execute("_MOVE");
    s.execute("#1");
    s.execute("0,0");
    s.execute("6,1");
    near(dynamic_cast<const Line&>(*d.entity(1)).start(), {7, 2});
    s.addViewportExample();
    EntityId cid = 0;
    for (auto id : d.entityIds())
        if (dynamic_cast<const Circle*>(d.entity(id)))
            cid = id;
    ASSERT_NE(cid, 0u);
    auto rev = d.revision();
    s.execute("_SCALE");
    s.execute(QString("#1 #%1").arg(cid));
    s.execute("0,0");
    EXPECT_TRUE(s.execute("XY 2 3").contains("uniform scaling"));
    EXPECT_EQ(d.revision(), rev);
    EXPECT_EQ(e.step(), Step::Factor);
    s.execute("2");
    EXPECT_EQ(d.revision(), rev + 1);
}
TEST(EditSession, ReferenceKeywordBeatsRedoAliasOnlyWhileScaleExpectsIt) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    auto path = temp.filePath("aliases.ini");
    {
        QSettings ini(path, QSettings::IniFormat);
        ini.setValue("aliases/R", "REDO");
    }
    CommandSession s;
    s.loadAliases(path);
    s.execute("_LINE 0,0 8,0");
    s.execute("_SCALE");
    s.execute("#1");
    s.execute("0,0");
    s.execute("r");
    ASSERT_EQ(s.editor().step(), Step::Reference);
    s.execute("8");
    s.execute("12,0");
    auto end = [&] { return dynamic_cast<const Line&>(*s.document().entity(1)).end(); };
    near(end(), {12, 0});
    s.execute("_UNDO");
    near(end(), {8, 0});
    s.execute("R");
    near(end(), {12, 0});
}
TEST(EditSession, ChangedDocumentInvalidatesGestureIncludingAfterResetWithReusedIds) {
    Document d;
    auto id = d.add(Line({0, 0}, {4, 1}));
    EditController e(d);
    e.beginGrip(id, 0);
    e.hover(Vec2{-2, 1});
    ASSERT_NE(e.ghost(), nullptr);
    auto fresh = std::make_unique<Document>();
    fresh->add(Circle({}, 8));
    d.replaceContents(std::move(fresh));
    EXPECT_FALSE(e.active());
    EXPECT_FALSE(e.ghost());
    e.enter({id});
    EXPECT_FALSE(d.canUndo());
    ASSERT_NE(dynamic_cast<const Circle*>(d.entity(id)), nullptr);
}
} // namespace
namespace {
TEST(PublishingSession, BilingualDimensionsUseTheSharedStyleAndHotDefPoints) {
    CommandSession s;
    Language lang;
    ASSERT_TRUE(lang.setRussian(true));
    EXPECT_TRUE(s.completions().contains("РАЗМРАДИУС"));
    EXPECT_TRUE(s.completions().contains("РАСЧЛЕНИТЬ"));
    s.execute("РАЗМСТИЛЬ");
    ASSERT_EQ(s.editor().step(), EditController::Step::StyleSpec);
    s.execute("TICK 1.5 3 0.2 0.8 0.4 1 L= mm");
    EXPECT_FALSE(s.editor().active());
    s.execute("РАЗМЛИНЕЙНЫЙ");
    s.execute("Y");
    s.execute("0,0");
    s.execute("3,4");
    s.editor().hover(pd::Vec2{8, 4});
    ASSERT_TRUE(s.editor().ghost());
    EXPECT_EQ(s.document().size(), 0u);
    s.execute("");
    auto& dim = dynamic_cast<const pd::Dimension&>(*s.document().entity(1));
    EXPECT_EQ(dim.label(), "L=4.0mm");
    EXPECT_EQ(dim.style().arrow, pd::DimStyle::Arrow::Tick);
    s.execute("_DIMSTYLE ISO-25");
    s.execute("_DIMDIAMETER 0,0 3,4 8,0");
    ASSERT_EQ(s.document().size(), 2u);
    EXPECT_EQ(dynamic_cast<const pd::Dimension&>(*s.document().entity(2)).label(), "Ø10.00");
    s.execute("_DIMANGULAR 0,0 0,4 4,0 -3,2");
    ASSERT_EQ(s.document().size(), 3u);
    EXPECT_EQ(dynamic_cast<const pd::Dimension&>(*s.document().entity(3)).label(), "270.00°");
    s.execute("РАЗМСТИЛЬ ЕСКД");
    s.execute("_DIMRADIUS 0,0 5,0 8,2");
    EXPECT_EQ(dynamic_cast<const pd::Dimension&>(*s.document().entity(4)).style(), pd::DimStyle::eskd());
    ASSERT_TRUE(lang.setRussian(false));
}
TEST(PublishingSession, NamedSheetViewportMarginsAndStampAreEditableWithoutChangingModelSpace) {
    CommandSession s;
    s.execute("_LAYOUT");
    s.execute("\"Сборка 1\" A3 P 1:100 200,400 PLAIN");
    ASSERT_EQ(s.space(), "Сборка 1");
    ASSERT_TRUE(s.activeLayout());
    s.execute("_LAYOUT VIEW 10,65 200,270 120,240 1:10");
    s.execute("_LAYOUT ADDVIEW 15,15 45,45 0,0 1:1");
    s.execute("_LAYOUT MARGINS 10 6 7 8");
    s.execute("_LAYOUT STAMP ПД.001 \"Сборочный чертёж\"");
    auto l = *s.activeLayout();
    EXPECT_EQ(l.paper, Paper::A3);
    EXPECT_FALSE(l.landscape);
    EXPECT_EQ(l.title, "Сборочный чертёж");
    ASSERT_EQ(l.viewports.size(), 2u);
    EXPECT_EQ(l.viewports[0].camera(), (Vec2{120, 240}));
    EXPECT_DOUBLE_EQ(l.viewports[0].scale(), .1);
    EXPECT_EQ(l.printable(), BoundingBox2D({10, 6}, {290, 412}));
    s.execute("_UNDO");
    EXPECT_FALSE(s.activeLayout()->stamp);
    s.execute("_LAYOUT");
    s.execute("MODEL");
    EXPECT_TRUE(s.space().isEmpty());
    EXPECT_FALSE(s.editor().active());
    EXPECT_EQ(s.document().size(), 0u);
}
TEST(PublishingSession, DefineInsertExplodeAndHatchFromInteractiveSelection) {
    CommandSession s;
    s.execute("_LINE 1,1 11,1");
    s.execute("_BLOCK");
    s.execute("#1");
    s.execute("Pin");
    s.execute("1,1");
    auto& d = s.document();
    ASSERT_EQ(d.blockIds().size(), 1u);
    ASSERT_EQ(d.size(), 1u);
    s.execute("_INSERT Pin 2 1 90");
    s.execute("20,10");
    ASSERT_EQ(d.size(), 2u);
    auto id = d.entityIds().back();
    s.execute(QString("_EXPLODE #%1").arg(id));
    auto& out = dynamic_cast<const pd::Line&>(*d.entity(d.entityIds().back()));
    near(out.start(), {20, 10});
    near(out.end(), {20, 30});
    s.execute("_UNDO");
    ASSERT_TRUE(dynamic_cast<const pd::BlockReference*>(d.entity(id)));
    s.addViewportExample();
    pd::EntityId circle = 0;
    for (auto eid : d.entityIds())
        if (dynamic_cast<const pd::Circle*>(d.entity(eid)))
            circle = eid;
    ASSERT_NE(circle, 0u);
    s.execute("_HATCH");
    s.execute(QString("#%1").arg(circle));
    Language lang;
    ASSERT_TRUE(lang.setRussian(true));
    s.execute("СЕТКА 0.5 20");
    ASSERT_TRUE(lang.setRussian(false));
    auto& h = dynamic_cast<const pd::Hatch&>(*d.entity(d.entityIds().back()));
    EXPECT_EQ(h.pattern(), pd::HatchPattern::Grid);
    EXPECT_GT(h.primitives().size(), 20u);
}
} // namespace

TEST(EditSession, ConstructionCommandsCreateExactClosedGeometry) {
    CommandSession s;
    auto& d = s.document();
    s.execute("_RECTANG 0,0 40,25");
    ASSERT_EQ(d.size(), 1u);
    auto& box = dynamic_cast<const Polyline&>(*d.entity(1));
    EXPECT_TRUE(box.closed());
    ASSERT_EQ(box.vertices().size(), 4u);
    EXPECT_EQ(d.extents().max(), (Vec2{40, 25}));
    s.execute("_CIRCLE 100,0 10");
    auto& c = dynamic_cast<const Circle&>(*d.entity(2));
    EXPECT_DOUBLE_EQ(c.radius(), 10);
    s.execute("_POLYGON 5 200,0 10,0");
    auto& penta = dynamic_cast<const Polyline&>(*d.entity(3));
    EXPECT_EQ(penta.vertices().size(), 5u);
    s.execute("_POLYGON 4 300,0 C 310,0");
    auto& square = dynamic_cast<const Polyline&>(*d.entity(4));
    for (std::size_t i = 0; i < square.vertices().size(); ++i) {
        auto a = square.vertices()[i].point, b = square.vertices()[(i + 1) % 4].point;
        EXPECT_NEAR((midpoint(a, b) - Vec2{300, 0}).length(), 10, 1e-9);
    }
    s.execute("_ARC 0,100 5,105 10,100");
    EXPECT_NEAR(dynamic_cast<const Arc&>(*d.entity(5)).curve().radius(), 5, 1e-9);
    s.execute("_PLINE 0,200 10,200 10,210 C");
    auto& closed = dynamic_cast<const Polyline&>(*d.entity(6));
    EXPECT_TRUE(closed.closed());
    EXPECT_EQ(closed.vertices().size(), 3u);
    s.execute("_PLINE 20,200 30,200");
    s.execute(""); // Enter ends an open polyline
    auto& open = dynamic_cast<const Polyline&>(*d.entity(7));
    EXPECT_FALSE(open.closed());
    EXPECT_EQ(open.vertices().size(), 2u);
    EXPECT_FALSE(s.execute("_POLYGON 2 0,0 1,0").isEmpty());
    EXPECT_EQ(d.size(), 7u);
    EXPECT_TRUE(d.undo());
    EXPECT_EQ(d.size(), 6u);
}

TEST(EditSession, ChamferArrayMatchPropDistanceAndAreaWorkAtThePrompt) {
    CommandSession s;
    auto& d = s.document();
    s.execute("_LINE 0,0 20,0");
    s.execute("_LINE 0,0 0,20");
    s.setSelectionProvider([] { return std::vector<EntityId>{1, 2}; });
    s.execute("_CHAMFER 5,2");
    s.execute("#1 10,0");
    auto reply = s.execute("#2 0,10");
    EXPECT_TRUE(reply.contains("completed")) << reply.toStdString();
    ASSERT_EQ(d.size(), 3u);
    EXPECT_NEAR(dynamic_cast<const Line&>(*d.entity(1)).start().x, 5, 1e-9);
    auto& connector = dynamic_cast<const Line&>(*d.entity(3));
    EXPECT_NEAR((connector.end() - connector.start()).length(), std::sqrt(29.), 1e-9);

    s.execute("_CIRCLE 100,0 5");
    auto circle = d.entityIds().back();
    s.setSelectionProvider([circle] { return std::vector<EntityId>{circle}; });
    s.execute("_ARRAY R 2 3 30 20");
    EXPECT_EQ(d.size(), 4u + 5u);
    EXPECT_TRUE(d.undo());
    EXPECT_EQ(d.size(), 4u);
    s.execute("_ARRAY P 100,0 4");
    EXPECT_EQ(d.size(), 4u + 3u);
    EXPECT_TRUE(d.undo());

    s.execute("_CIRCLE 50,50 4");
    auto source = d.entityIds().back();
    s.execute("_LINE 200,0 210,0");
    auto targetId = d.entityIds().back();
    Layer red("Red");
    red.color = Color{250, 5, 5};
    LayerId lid = 0;
    d.transact([&](Transaction& tx) { lid = tx.addLayer(red); });
    d.transact([&](Transaction& tx) {
        auto l = d.entity(source)->clone();
        l->traits.layer = lid;
        l->traits.color = Color{1, 2, 3};
        l->traits.lineWeight = .7;
        tx.replace(source, *l);
    });
    s.setSelectionProvider([targetId] { return std::vector<EntityId>{targetId}; });
    s.execute(QString("_MATCHPROP #%1").arg(source));
    auto& target = dynamic_cast<const Line&>(*d.entity(targetId));
    EXPECT_EQ(target.traits.layer, lid);
    EXPECT_EQ(std::get<Color>(target.traits.color), (Color{1, 2, 3}));
    EXPECT_DOUBLE_EQ(std::get<double>(target.traits.lineWeight), .7);

    auto distance = s.execute("_DIST 0,0 3,4");
    EXPECT_TRUE(distance.contains("5.000")) << distance.toStdString();
    EXPECT_TRUE(distance.contains("53.13")) << distance.toStdString();
    auto areaReply = s.execute(QString("_AREA #%1").arg(source));
    EXPECT_TRUE(areaReply.contains("mm²")) << areaReply.toStdString();
    auto value = areaReply.mid(areaReply.indexOf(' ') + 1);
    EXPECT_NEAR(std::stod(value.toStdString()), std::numbers::pi * 16, .01);
}

TEST(EditSession, ShortAliasesResolveInEnglishAndRussian) {
    CommandSession s;
    auto& d = s.document();
    s.execute("L 0,0 10,0");
    ASSERT_EQ(d.size(), 1u);
    ASSERT_NE(dynamic_cast<const Line*>(d.entity(1)), nullptr);
    s.execute("ОТ 0,10 10,10");
    EXPECT_EQ(d.size(), 2u);
    s.execute("REC 20,0 30,10");
    EXPECT_EQ(d.size(), 3u);
    EXPECT_TRUE(dynamic_cast<const Polyline&>(*d.entity(d.entityIds().back())).closed());
    s.execute("К 40,0 5");
    EXPECT_DOUBLE_EQ(dynamic_cast<const Circle&>(*d.entity(d.entityIds().back())).radius(), 5);
    s.execute("PL 50,0 60,0 60,10");
    s.execute("");
    EXPECT_EQ(d.size(), 5u);
    EXPECT_FALSE(dynamic_cast<const Polyline&>(*d.entity(d.entityIds().back())).closed());
    s.execute("Д 100,0 105,5 110,0");
    EXPECT_EQ(d.size(), 6u);
    s.setSelectionProvider([] { return std::vector<EntityId>{1}; });
    s.execute("M 0,0 0,5");
    EXPECT_NEAR(dynamic_cast<const Line&>(*d.entity(1)).start().y, 5, 1e-9);
    s.execute("CO 0,0 10,0");
    ASSERT_EQ(d.size(), 7u);
    auto copy = d.entityIds().back();
    s.execute(QString("E #%1").arg(copy));
    EXPECT_EQ(d.size(), 6u);
    EXPECT_TRUE(d.undo());
    EXPECT_EQ(d.size(), 7u);
    // An unknown name fails cleanly, and the built-in table survives an empty user file.
    EXPECT_NE(s.execute("LISPX"), QString{});
    EXPECT_EQ(d.size(), 7u);
    s.loadAliases(QStringLiteral("/nonexistent/planedraft-aliases.ini"));
    EXPECT_EQ(CommandSession::defaultAliases().value("ОБР"), QStringLiteral("TRIM"));
    s.execute("ОТ 0,20 10,20");
    EXPECT_EQ(d.size(), 8u);
}


TEST(EditSession, RussianNamesOfTheNewCommandsResolve) {
    CommandSession s;
    auto& d = s.document();
    Language lang;
    ASSERT_TRUE(lang.setRussian(true));
    for (auto name : {"ПРЯМОУГ", "МН-УГОЛ", "ФАСКА", "МАССИВ", "КОПИРОВАТЬСВ", "ДИСТ",
                      "ПЛОЩАДЬ", "КРУГ", "ДУГА", "ПОЛИЛИНИЯ", "ПОКАЗАТЬ", "КОПИРОВАТЬВБУФЕР",
                      "ВЫРЕЗАТЬВБУФЕР", "ВСТАВИТЬИЗБУФЕРА"})
        EXPECT_TRUE(s.completions().contains(QString::fromUtf8(name))) << name;
    s.execute("ПРЯМОУГ 0,0 20,10");
    ASSERT_EQ(d.size(), 1u);
    EXPECT_TRUE(dynamic_cast<const Polyline&>(*d.entity(1)).closed());
    s.execute("КРУГ 100,0 5");
    EXPECT_DOUBLE_EQ(dynamic_cast<const Circle&>(*d.entity(2)).radius(), 5);
    s.execute("МН-УГОЛ 5 200,0 10,0");
    EXPECT_EQ(dynamic_cast<const Polyline&>(*d.entity(3)).vertices().size(), 5u);
    auto distance = s.execute("ДИСТ 0,0 3,4");
    EXPECT_TRUE(distance.contains("5.000")) << distance.toStdString();
    auto area = s.execute("ПЛОЩАДЬ #1");
    EXPECT_TRUE(area.contains("40")) << area.toStdString();
    s.setSelectionProvider([] { return std::vector<EntityId>{1}; });
    s.execute("КОПИРОВАТЬВБУФЕР");
    ASSERT_FALSE(s.clipboardEmpty());
    s.execute("ВСТАВИТЬИЗБУФЕРА 0,50");
    EXPECT_EQ(d.size(), 4u);
    s.setSelectionProvider([] { return std::vector<EntityId>{4}; });
    s.execute("ВЫРЕЗАТЬВБУФЕР");
    EXPECT_EQ(d.size(), 3u);
    auto missing = s.execute("ПОКАЗАТЬ");
    EXPECT_FALSE(missing.isEmpty());
    ASSERT_TRUE(lang.setRussian(false));
}

TEST(EditSession, ZoomIsRoutedToTheActiveViewport) {
    CommandSession s;
    int calls = 0;
    s.setZoomExtentsHandler([&] { ++calls; });
    EXPECT_NE(s.execute("Z").toStdString().find("extents"), std::string::npos);
    EXPECT_EQ(calls, 1);
    s.execute("ZOOM E");
    EXPECT_EQ(calls, 2);
    EXPECT_NE(s.execute("ZOOM W").toStdString().find("ZOOM E"), std::string::npos);
    EXPECT_EQ(calls, 2);
    s.setZoomExtentsHandler({});
    EXPECT_NE(s.execute("Z").toStdString().find("drawing window"), std::string::npos);
}
