// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include "language.h"
#include "main_window.h"
#include "pointer_input.hpp"
#include <QAction>
#include <QFile>
#include <QLineEdit>
#include <QTabBar>
#include <QTemporaryDir>
#include <gtest/gtest.h>
namespace {
using namespace input_test;
TEST(EditorWindow, EscapeDuringMoveOrGripPreservesGeometryBothHistoryBranchesAndSelection) {
    CommandSession s;
    Language lang;
    MainWindow window(s, lang);
    auto& v = *window.findChild<Viewport*>();
    auto& e = s.editor();
    auto& d = s.document();
    s.execute("_LINE -20,0 20,0");
    s.execute("_LINE 0,6 6,6");
    s.execute("_UNDO");
    v.zoomExtents();
    click(v, {0, 0});
    ASSERT_EQ(v.selectedIds(), std::vector<pd::EntityId>{1});
    auto rev = d.revision();
    int notices = 0;
    QObject::connect(&s, &CommandSession::documentChanged, &window, [&] { ++notices; });
    s.execute("_MOVE");
    click(v, {0, 0});
    move(v, {0, 8});
    ASSERT_NE(e.ghost(), nullptr);
    EXPECT_TRUE(d.query({{0, 7}, {1, 9}}).empty());
    key(*window.findChild<QLineEdit*>("commandInput"), Qt::Key_Escape);
    EXPECT_FALSE(e.active());
    EXPECT_FALSE(e.ghost());
    EXPECT_EQ(v.selectedIds(), std::vector<pd::EntityId>{1});
    press(v, {20, 0});
    ASSERT_EQ(e.hotId(), 1u);
    move(v, {12, 6}, true);
    ASSERT_NE(e.ghost(), nullptr);
    key(v, Qt::Key_Escape);
    release(v, {12, 6});
    EXPECT_FALSE(e.active());
    EXPECT_FALSE(e.ghost());
    EXPECT_EQ(d.revision(), rev);
    EXPECT_EQ(notices, 0);
    EXPECT_TRUE(d.canRedo());
    EXPECT_EQ(dynamic_cast<const pd::Line&>(*d.entity(1)).end(), (pd::Vec2{20, 0}));
    s.execute("_UNDO");
    EXPECT_EQ(d.size(), 0u);
    s.execute("_REDO");
    s.execute("_REDO");
    EXPECT_EQ(d.size(), 2u);
}
TEST(EditorWindow, VerbNounAccumulatesMousePicksThenEnterAndDeleteIsAtomic) {
    CommandSession s;
    Language lang;
    MainWindow window(s, lang);
    auto& v = *window.findChild<Viewport*>();
    auto& d = s.document();
    s.execute("_LINE -20,-5 20,-5");
    s.execute("_LINE -20,5 20,5");
    v.zoomExtents();
    auto rev = d.revision();
    s.execute("_MOVE");
    click(v, {0, -5});
    click(v, {0, 5});
    ASSERT_EQ(v.selectedIds().size(), 2u);
    key(v, Qt::Key_Return);
    ASSERT_EQ(s.editor().step(), EditController::Step::Base);
    s.execute("0,0");
    s.execute("4,7");
    EXPECT_EQ(d.revision(), rev + 1);
    EXPECT_EQ(dynamic_cast<const pd::Line&>(*d.entity(1)).start(), (pd::Vec2{-16, 2}));
    key(v, Qt::Key_Delete);
    EXPECT_EQ(d.size(), 0u);
    EXPECT_EQ(d.revision(), rev + 2);
    s.execute("_UNDO");
    EXPECT_EQ(d.size(), 2u);
}
TEST(EditorWindow, ClickingMidpointMakesItHotUntilClickOrEnterMovesTheWholeLine) {
    CommandSession s;
    Language lang;
    MainWindow w(s, lang);
    auto& v = *w.findChild<Viewport*>();
    s.execute("_LINE 0,0 10,0");
    v.zoomExtents();
    click(v, {3, 0});
    for (bool confirmByKey : {false, true}) {
        auto rev = s.document().revision();
        click(v, {5, 0});
        ASSERT_EQ(s.editor().hotId(), 1u);
        move(v, {6, 2});
        ASSERT_NE(s.editor().ghost(), nullptr);
        EXPECT_EQ(s.document().revision(), rev);
        if (confirmByKey)
            key(v, Qt::Key_Return);
        else
            click(v, {6, 2});
        ASSERT_FALSE(s.editor().active());
        EXPECT_EQ(s.document().revision(), rev + 1);
        auto& out = dynamic_cast<const pd::Line&>(*s.document().entity(1));
        EXPECT_NEAR(out.start().x, 1, pd::EPSILON);
        EXPECT_NEAR(out.start().y, 2, pd::EPSILON);
        EXPECT_NEAR(out.end().x, 11, pd::EPSILON);
        EXPECT_NEAR(out.end().y, 2, pd::EPSILON);
        s.execute("_UNDO");
        EXPECT_EQ(dynamic_cast<const pd::Line&>(*s.document().entity(1)).end(), (pd::Vec2{10, 0}));
    }
}
TEST(EditorWindow, OffsetSideInsideSnapApertureUsesTheActualClick) {
    CommandSession s;
    Language lang;
    MainWindow w(s, lang);
    auto& v = *w.findChild<Viewport*>();
    s.execute("_LINE -10,0 10,0");
    v.zoomExtents();
    click(v, {0, 0});
    s.execute("_OFFSET");
    s.execute("3");
    click(v, {0, -2 / v.camera().scale()});
    ASSERT_FALSE(s.editor().active());
    ASSERT_EQ(s.document().size(), 2u);
    EXPECT_NEAR(dynamic_cast<const pd::Line&>(*s.document().entity(2)).end().y, -3, pd::EPSILON);
}
TEST(EditorWindow, PrecisionActionsConstrainTheSameSnappedPointForPreviewAndCommit) {
    CommandSession s;
    Language lang;
    MainWindow window(s, lang);
    auto& v = *window.findChild<Viewport*>();
    auto& e = s.editor();
    auto action = [&](const QString& name) -> QAction* {
        for (auto a : window.findChildren<QAction*>())
            if (a->text() == name)
                return a;
        return nullptr;
    };
    auto* ortho = action("Ortho");
    auto* polar = action("Polar tracking");
    ASSERT_TRUE(ortho && polar);
    EXPECT_EQ(ortho->shortcut(), QKeySequence(Qt::Key_F8));
    EXPECT_EQ(polar->shortcut(), QKeySequence(Qt::Key_F10));
    ortho->trigger();
    EXPECT_EQ(e.tracking(), pd::edit::Tracking::Ortho);
    s.execute("_LINE 0,0 10,0");
    s.execute("_LINE 24,4 30,4");
    v.zoomExtents();
    click(v, {5, 0});
    press(v, {10, 0});
    move(v, {23.9, 4.1}, true);
    ASSERT_NE(e.ghost(), nullptr);
    auto& ghost = dynamic_cast<const pd::Line&>(*e.ghost()->items()[0].after);
    EXPECT_EQ(ghost.end(), (pd::Vec2{24, 0}));
    release(v, {23.9, 4.1});
    EXPECT_EQ(dynamic_cast<const pd::Line&>(*s.document().entity(1)).end(), (pd::Vec2{24, 0}));
    auto* increment = action("45°");
    ASSERT_NE(increment, nullptr);
    increment->trigger();
    polar->trigger();
    ASSERT_TRUE(polar->isChecked());
    EXPECT_FALSE(ortho->isChecked());
    s.execute("_LINE");
    s.execute("0,0");
    move(v, {6, 6.5});
    ASSERT_TRUE(e.guideBase());
    auto tracked = *e.cursorPoint();
    EXPECT_NEAR(tracked.x, tracked.y, pd::EPSILON);
    key(v, Qt::Key_Return);
    ASSERT_EQ(s.document().size(), 3u);
    EXPECT_EQ(dynamic_cast<const pd::Line&>(*s.document().entity(3)).end(), tracked);
}
} // namespace
TEST(PublishingWindow, LayoutTabsAreViewsNotModelTransactionsAndExportTheActivePaper) {
    CommandSession s;
    Language lang;
    MainWindow w(s, lang);
    auto* tabs = w.findChild<QTabBar*>("spaceTabs");
    auto* v = w.findChild<Viewport*>();
    ASSERT_TRUE(tabs && v);
    ASSERT_EQ(tabs->count(), 2);
    ASSERT_TRUE(lang.setRussian(true));
    QApplication::processEvents();
    EXPECT_EQ(tabs->tabText(0), "Модель");
    EXPECT_EQ(tabs->tabText(1), "Лист 1");
    s.execute("_LINE 0,0 80,0");
    s.execute("ЛИСТ План A4 L 1:2 40,0");
    ASSERT_TRUE(v->paperSpace());
    ASSERT_EQ(s.space(), "План");
    auto rev = s.document().revision();
    s.execute("_LINE 2,3 4,6");
    EXPECT_EQ(s.document().revision(), rev);
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    auto path = tmp.filePath("лист.svg");
    s.execute("_EXPORTSVG \"" + path + "\"");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_TRUE(f.readAll().contains("width=\"297mm\" height=\"210mm\""));
    tabs->setCurrentIndex(0);
    ASSERT_FALSE(v->paperSpace());
    tabs->setCurrentIndex(1);
    ASSERT_TRUE(v->paperSpace());
    EXPECT_EQ(s.document().revision(), rev);
    s.execute("_UNDO");
    EXPECT_FALSE(v->paperSpace());
    EXPECT_EQ(s.document().layoutIds().size(), 0u);
    ASSERT_TRUE(lang.setRussian(false));
}
