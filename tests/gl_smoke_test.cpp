// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include "language.h"
#include "main_window.h"
#include "pointer_input.hpp"
#include "sheet_example.hpp"
#include "viewport.h"
#include <QSettings>
#include <QTabBar>
#include <QTableWidget>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QTemporaryDir>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>

static int countInk(const QImage& image, pd::Color c) {
    int n = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const auto p = image.pixel(x, y);
            if (std::abs(qRed(p) - c.r) <= 2 && std::abs(qGreen(p) - c.g) <= 2 &&
                std::abs(qBlue(p) - c.b) <= 2) {
                ++n;
            }
        }
    }
    return n;
}

// Opt-in integration test for a desktop GL machine: exercises the REAL widget/FBO and shader resources.
TEST(ViewportGL, ReadBackGridGeometryGlyphsGripsAndUndoAtLargeCoordinates) {
    using namespace pd;
    QOpenGLContext probe;
    probe.setFormat(Viewport::requestedFormat());
    ASSERT_TRUE(probe.create()) << "Run the framebuffer test in a desktop GL 4.5 session.";
    ASSERT_FALSE(probe.isOpenGLES());
    ASSERT_EQ(probe.format().profile(), QSurfaceFormat::CoreProfile);
    ASSERT_GE(probe.format().version(), qMakePair(4, 5));
    Document doc;
    auto place = Matrix3x3::translation({1e7, -1e7});
    constexpr Color red{224, 57, 71}, cyan{54, 203, 211}, amber{245, 173, 52}, purple{181, 74, 216},
        mint{85, 225, 160};
    doc.transact([&](Transaction& tx) {
        Layer layer("lines");
        layer.color = red;
        auto id = tx.addLayer(layer);
        auto add = [&](Entity&& e, Color color) {
            e.traits.color = color;
            e.transform(place);
            tx.add(e);
        };
        Line line({-20, 0}, {20, 0});
        line.traits.layer = id;
        line.transform(place);
        tx.add(line);
        add(Circle({12, 9}, 4), cyan);
        add(Arc(CircularArc({-10, 7}, 4, 0, 3.5)), amber);
        add(Polyline({{{-15, -8}, 1}, {{-5, -8}, 0}}), purple);
        add(Text("CAD", {{0, -12}, {15, -5}}, {}), mint);
    });
    Viewport view(doc);
    QString error;
    QObject::connect(&view, &Viewport::rendererError, &view, [&](auto e) { error = e; });
    auto token = doc.observe([&](auto changes) { view.invalidate(changes); });
    // With 150% scaling both dimensions need rounding up, even with a different desktop style.
    view.resize(481, 321);
    view.show();
    QApplication::processEvents();
    view.zoomExtents();
    auto image = view.grabFramebuffer();
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    ASSERT_TRUE(view.isValid());
    ASSERT_FALSE(image.isNull());
    view.makeCurrent();
    auto* gl = view.context()->functions();
    GLint vp[4];
    gl->glGetIntegerv(GL_VIEWPORT, vp);
    EXPECT_EQ(image.size(), view.size() * view.devicePixelRatioF());
    EXPECT_EQ(QSize(vp[2], vp[3]), image.size());
    std::cout << "GL: " << gl->glGetString(GL_RENDERER) << " / " << gl->glGetString(GL_VERSION) << '\n';
    EXPECT_EQ(gl->glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    view.doneCurrent();
    for (auto c : {red, cyan, amber, purple, mint}) {
        EXPECT_GT(countInk(image, c), 8) << "missing ink " << int(c.r);
    }
    auto p = view.camera().worldToScreen(place.applyPoint({}));
    QPointF s(p.x, p.y);
    QMouseEvent press(QEvent::MouseButtonPress, s, s, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, s, s, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &release);
    ASSERT_EQ(view.selection().ids().size(), 1u);
    // Do not let the horizontal arm of the crosshair cover the selected horizontal test line.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&view, &leave);
    image = view.grabFramebuffer();
    const Color selected{selectionInk.r, selectionInk.g, selectionInk.b},
        grip{gripInk.r, gripInk.g, gripInk.b};
    EXPECT_GT(countInk(image, selected), 8);
    EXPECT_GT(countInk(image, grip), 8);
    ASSERT_TRUE(doc.undo());
    image = view.grabFramebuffer();
    for (auto c : {red, cyan, amber, purple, mint, selected, grip}) {
        EXPECT_EQ(countInk(image, c), 0);
    }
    // No drawing, no cursor: the infinite grid itself still has varying pixel values.
    const auto bg = image.pixel(0, 0);
    bool grid = false;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            grid |= image.pixel(x, y) != bg;
        }
    }
    EXPECT_TRUE(grid);
    ASSERT_TRUE(doc.redo());
    image = view.grabFramebuffer();
    EXPECT_GT(countInk(image, cyan), 8);
}
TEST(ViewportGL, NativeCommandInvalidatesRealBuffersOnCommitUndoAndRedo) {
    CommandSession session;
    Viewport view(session.document());
    QObject::connect(&session, &CommandSession::documentChanged, &view,
                     [&](const QList<pd::Change>& changes) {
                         view.invalidate({changes.constData(), static_cast<size_t>(changes.size())});
                     });
    view.resize(480, 320);
    view.show();
    QApplication::processEvents();
    ASSERT_TRUE(session.loadPlugin(QString::fromUtf8(PD_FRAME_PATH)).isEmpty());
    session.execute("_FRAME 64");
    view.zoomExtents();
    constexpr pd::Color frameInk{86, 202, 220};
    auto image = view.grabFramebuffer();
    ASSERT_FALSE(image.isNull());
    EXPECT_GT(countInk(image, frameInk), 100);
    ASSERT_TRUE(session.unloadPlugin(QString::fromUtf8(PD_FRAME_PATH)).isEmpty());
    session.execute("_UNDO");
    EXPECT_EQ(countInk(view.grabFramebuffer(), frameInk), 0);
    session.execute("_REDO");
    EXPECT_GT(countInk(view.grabFramebuffer(), frameInk), 100);
}
TEST(ViewportGL, OpenReuploadsEvenWhenDocumentIDsBoundsAndCameraCoincide) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    CommandSession session;
    Viewport view(session.document());
    QObject::connect(&session, &CommandSession::documentChanged, &view, [&](const QList<pd::Change>& batch) {
        view.invalidate({batch.constData(), static_cast<size_t>(batch.size())});
    });
    view.resize(480, 320);
    view.show();
    QApplication::processEvents();
    const pd::Color red{221, 45, 53}, green{63, 201, 105};
    auto load = [&](pd::Color c) {
        pd::Document source;
        pd::Line line({1e7, 1e7}, {1e7 + 80, 1e7 + 15});
        line.traits.color = c;
        source.add(line);
        auto p = tmp.filePath("page.dxf");
        ASSERT_TRUE(pd::io::IoRegistry{}.write(std::filesystem::path(p.toStdU16String()), source).ok());
        session.execute("_OPEN " + p);
    };
    load(red);
    auto first = view.grabFramebuffer();
    ASSERT_GT(countInk(first, red), 100);
    load(green);
    auto second = view.grabFramebuffer();
    ASSERT_FALSE(second.isNull());
    EXPECT_EQ(countInk(second, red), 0);
    EXPECT_GT(countInk(second, green), 100);
}
TEST(ViewportGL, GripGhostIsOverlayOnlyAndTheCommittedStrokeFollowsUndoRedo) {
    using namespace input_test;
    using pd::Vec2;
    CommandSession session;
    Viewport v(session.document());
    v.setEditor(&session.editor());
    QObject::connect(&session, &CommandSession::documentChanged, &v, [&](const QList<pd::Change>& batch) {
        v.invalidate({batch.constData(), static_cast<size_t>(batch.size())});
    });
    session.execute("_LINE -20,-4 20,-4");
    v.resize(480, 320);
    v.show();
    QApplication::processEvents();
    v.zoomExtents();
    click(v, {0, -4});
    ASSERT_EQ(v.selectedIds(), std::vector<pd::EntityId>{1});
    leave(v);
    auto before = v.grabFramebuffer();
    ASSERT_FALSE(before.isNull());
    auto rev = session.document().revision();
    Vec2 end{20, -4}, dest{10, 6};
    auto sample = v.camera().worldToScreen({-10, -4 + 10. / 3});
    double dpi = double(before.width()) / v.width();
    int sx = static_cast<int>(sample.x * dpi), sy = static_cast<int>(sample.y * dpi);
    auto patch = [&](const QImage& image) { return image.copy(sx - 6, sy - 6, 13, 13); };
    auto blank = patch(before);
    press(v, end);
    move(v, dest, true);
    auto ghost = v.grabFramebuffer();
    ASSERT_EQ(session.document().revision(), rev);
    EXPECT_NE(patch(ghost), blank);
    EXPECT_GT(countInk(ghost, {pd::hotGripInk.r, pd::hotGripInk.g, pd::hotGripInk.b}), 8);
    EXPECT_EQ(dynamic_cast<const pd::Line&>(*session.document().entity(1)).end(), end);
    key(v, Qt::Key_Escape);
    release(v, dest);
    leave(v);
    EXPECT_EQ(v.grabFramebuffer(), before);
    EXPECT_EQ(session.document().revision(), rev);
    press(v, end);
    move(v, dest, true);
    release(v, dest);
    leave(v);
    auto committed = v.grabFramebuffer();
    ASSERT_EQ(session.document().revision(), rev + 1);
    auto out = dynamic_cast<const pd::Line&>(*session.document().entity(1)).end();
    EXPECT_NEAR(out.x, dest.x, pd::EPSILON);
    EXPECT_NEAR(out.y, dest.y, pd::EPSILON);
    EXPECT_GT(countInk(patch(committed), {pd::selectionInk.r, pd::selectionInk.g, pd::selectionInk.b}), 2);
    EXPECT_NE(patch(committed), patch(ghost));
    session.execute("_UNDO");
    EXPECT_EQ(v.grabFramebuffer(), before);
    session.execute("_REDO");
    EXPECT_EQ(v.grabFramebuffer(), committed);
}
namespace {
QImage patchAt(const Viewport& v, const QImage& img, pd::Vec2 p) {
    auto logical = v.camera().worldToScreen(p);
    double ratio = double(img.width()) / v.width();
    return img.copy(int(logical.x * ratio) - 3, int(logical.y * ratio) - 3, 7, 7);
}
TEST(ViewportGL, InspectorEditsRepaintBothSelectedEntitiesAndGridToggleDoesNotTouchHistory) {
    using namespace pd;
    CommandSession s;
    Language lang;
    MainWindow window(s, lang);
    auto& d = s.document();
    auto a = d.add(Line({-16, -5}, {16, -5})), b = d.add(Line({-16, 5}, {16, 5}));
    window.resize(960, 640);
    window.show();
    QApplication::processEvents();
    auto& v = *window.activeView();
    v.zoomExtents();
    input_test::leave(v);
    auto baseline = v.grabFramebuffer();
    ASSERT_FALSE(baseline.isNull());
    auto grid = window.findChild<QAction*>("gridMode");
    ASSERT_NE(grid, nullptr);
    auto rev = d.revision();
    grid->trigger();
    EXPECT_NE(baseline, v.grabFramebuffer());
    grid->trigger();
    ASSERT_EQ(baseline, v.grabFramebuffer());
    EXPECT_EQ(d.revision(), rev);
    v.setSelection({a, b});
    auto table = window.findChild<QTableWidget*>("propertyTable");
    ASSERT_NE(table, nullptr);
    auto items = table->findItems("Color · RGB", Qt::MatchExactly);
    ASSERT_EQ(items.size(), 1);
    table->item(items.front()->row(), 1)->setText("#20AC79");
    v.setSelection({});
    auto edited = v.grabFramebuffer();
    Color ink{32, 172, 121};
    for (double y : {-5., 5.})
        EXPECT_GT(countInk(patchAt(v, edited, {0, y}), ink), 2);
    EXPECT_EQ(d.revision(), rev + 1);
    s.execute("_UNDO");
    EXPECT_EQ(v.grabFramebuffer(), baseline);
    s.execute("_REDO");
    EXPECT_EQ(v.grabFramebuffer(), edited);
}
TEST(ViewportGL, TabSwitchDisplaysPhysicalSheetAndClipsTheScaledModelAtBothEdges) {
    pd::Document d;
    sheet_test::drawing(d);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto path = dir.filePath("plate.dxf");
    ASSERT_TRUE(pd::io::IoRegistry{}.write(path.toStdString(), d).ok());
    CommandSession s;
    Language lang;
    MainWindow w(s, lang);
    s.execute("_OPEN " + path);
    w.resize(720, 560);
    w.show();
    QApplication::processEvents();
    auto& v = *w.findChild<Viewport*>();
    auto* tabs = w.findChild<QTabBar*>("spaceTabs");
    ASSERT_EQ(tabs->count(), 3);
    pd::Camera2D expected;
    expected.resize(v.width(), v.height());
    ASSERT_TRUE(expected.fit(s.document().extents()));
    EXPECT_EQ(v.camera().eye(), expected.eye()); // _OPEN before first show must fit the actual canvas.
    EXPECT_DOUBLE_EQ(v.camera().scale(), expected.scale());
    input_test::leave(v);
    auto model = v.grabFramebuffer();
    constexpr pd::Color red{230, 20, 20};
    ASSERT_GT(countInk(model, red), 25);
    auto rev = s.document().revision();
    tabs->setCurrentIndex(1);
    ASSERT_EQ(s.space(), "Plate");
    auto page = v.grabFramebuffer();
    ASSERT_FALSE(page.isNull());
    EXPECT_NE(model, page);
    EXPECT_GT(countInk(patchAt(v, page, {80, 95}), red), 2);
    EXPECT_GT(countInk(patchAt(v, page, {135, 98}), red), 2);
    for (auto p : {pd::Vec2{55, 95}, pd::Vec2{175, 95}}) {
        auto area = patchAt(v, page, p);
        EXPECT_EQ(countInk(area, red), 0);
        EXPECT_GT(countInk(area, {255, 255, 255}), 40);
    }
    tabs->setCurrentIndex(0);
    input_test::leave(v);
    EXPECT_EQ(v.grabFramebuffer(), model);
    EXPECT_EQ(s.document().revision(), rev);
}
TEST(ViewportGL, SolidWithIslandAndDimensionInkAtSurveyCoordinates) {
    using namespace pd;
    Document d;
    auto outer = Polyline({{{-20, -15}, 0}, {{20, -15}, 0}, {{20, 15}, 0}, {{-20, 15}, 0}}, true);
    Hatch hatch({outer, Hatch::boundary(Circle({}, 7))}, HatchPattern::Solid);
    constexpr Color green{83, 211, 97}, orange{244, 149, 52};
    hatch.traits.color = green;
    hatch.transform(Matrix3x3::translation(sheet_test::centre));
    d.add(hatch);
    Dimension dim(DimKind::Aligned, {{-20, 15}, {20, 15}, {0, 23}});
    dim.traits.color = orange;
    dim.transform(Matrix3x3::translation(sheet_test::centre));
    d.add(dim);
    Viewport v(d);
    v.resize(480, 360);
    v.show();
    QApplication::processEvents();
    v.zoomExtents();
    input_test::leave(v);
    auto frame = v.grabFramebuffer();
    ASSERT_FALSE(frame.isNull());
    EXPECT_GT(countInk(frame, green), 2000);
    EXPECT_GT(countInk(frame, orange), 40);
    EXPECT_EQ(countInk(patchAt(v, frame, sheet_test::centre), green), 0);
    EXPECT_GT(countInk(patchAt(v, frame, sheet_test::centre + Vec2{12, 2}), green), 45);
}
} // namespace
int main(int argc, char** argv) {
    QSurfaceFormat::setDefaultFormat(Viewport::requestedFormat());
    QApplication app(argc, argv);
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    app.setOrganizationName("PlaneDraftTests");
    app.setApplicationName("Framebuffer");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
