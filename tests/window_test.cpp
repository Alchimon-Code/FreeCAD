// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include "language.h"
#include "main_window.h"
#include "viewport.h"
#include <QMouseEvent>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(Window, MenuSwitchRetranslatesTheSameWindowAndPreservesInput) {
    CommandSession session;
    Language language;
    MainWindow window(session, language); // Never shown: no OpenGL context needed.
    auto* input = window.findChild<QLineEdit*>("commandInput");
    ASSERT_NE(input, nullptr);
    input->setText("_LINE 0,0 4,5");
    ASSERT_TRUE(QMetaObject::invokeMethod(input, "returnPressed"));
    input->setText("_LINE 8,9");

    auto select = [&](const QString& label) {
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == label) {
                action->trigger();
                QApplication::processEvents();
                return true;
            }
        }
        return false;
    };
    const auto englishTitle = window.windowTitle();
    ASSERT_TRUE(select("Русский"));
    EXPECT_TRUE(window.windowTitle().contains("Без имени"));
    EXPECT_TRUE(input->placeholderText().contains("ОТРЕЗОК"));
    EXPECT_TRUE(language.russian());
    QStringList labels;
    for (auto* action : window.findChildren<QAction*>())
        labels << action->text();
    for (auto label : {"Открыть…", "Сохранить", "Сохранить как…", "Экспорт в SVG…", "Экспорт в PDF…"})
        EXPECT_TRUE(labels.contains(label)) << label;
    EXPECT_EQ(QSettings().value("ui/language").toString(), "ru");
    ASSERT_TRUE(select("English"));
    EXPECT_EQ(window.windowTitle(), englishTitle);
    EXPECT_EQ(input->text(), "_LINE 8,9");
    EXPECT_EQ(session.document().size(), 1u);
}

TEST(Window, InteractiveLineUsesSnapAndStationaryCursorUpdatesOnHistoryPackets) {
    CommandSession session;
    Language language;
    MainWindow window(session, language);
    auto* view = window.findChild<Viewport*>();
    ASSERT_NE(view, nullptr);
    session.execute("_LINE -10,0 10,0");
    view->zoomExtents();
    auto mouse = [&](QEvent::Type type, pd::Vec2 screen, Qt::MouseButton button, Qt::MouseButtons down) {
        QPointF s(screen.x, screen.y);
        QMouseEvent event(type, s, s, button, down, Qt::NoModifier);
        QApplication::sendEvent(view, &event);
    };
    auto click = [&](pd::Vec2 s) {
        mouse(QEvent::MouseButtonPress, s, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, s, Qt::LeftButton, Qt::NoButton);
    };
    const auto left = view->camera().worldToScreen({-10, 0}), mid = view->camera().worldToScreen({0, 0});
    mouse(QEvent::MouseMove, left + pd::Vec2{1, 1}, Qt::NoButton, Qt::NoButton);
    ASSERT_TRUE(view->snapHit());
    EXPECT_EQ(view->snapHit()->type, pd::SnapType::Endpoint);
    session.execute("_UNDO");
    EXPECT_FALSE(view->snapHit());
    session.execute("_REDO");
    ASSERT_TRUE(view->snapHit());
    click(mid);
    EXPECT_EQ(view->selection().ids().size(), 1u);
    session.execute("_UNDO");
    EXPECT_TRUE(view->selection().ids().empty());
    session.execute("_REDO");
    // Drag across just the centre: left->right rejects the partially enclosed line, reverse accepts it.
    auto a = mid - pd::Vec2{13, 10}, b = mid + pd::Vec2{13, 10};
    auto drag = [&](pd::Vec2 s, pd::Vec2 t) {
        mouse(QEvent::MouseButtonPress, s, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, t, Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, t, Qt::LeftButton, Qt::NoButton);
    };
    drag(a, b);
    EXPECT_TRUE(view->selection().ids().empty());
    drag(b, a);
    EXPECT_EQ(view->selection().ids().size(), 1u);
    session.execute("_LINE");
    click(left + pd::Vec2{1, 2});
    click(mid + pd::Vec2{2, -1});
    ASSERT_EQ(session.document().size(), 2u);
    const auto* result = dynamic_cast<const pd::Line*>(session.document().entity(2));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->start(), (pd::Vec2{-10, 0}));
    EXPECT_EQ(result->end(), (pd::Vec2{0, 0}));
    session.execute("_UNDO");
    EXPECT_EQ(session.document().size(), 1u);
}

TEST(Window, ScriptReadsActualViewportSelectionAndWritesToConsole) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    CommandSession session;
    Language language;
    MainWindow window(session, language);
    session.execute("_LINE -10,0 10,0");
    session.execute("_LINE -10,8 10,8");
    auto* view = window.findChild<Viewport*>();
    ASSERT_NE(view, nullptr);
    view->zoomExtents();
    const auto p = view->camera().worldToScreen({0, 0});
    const QPointF s(p.x, p.y);
    QMouseEvent press(QEvent::MouseButtonPress, s, s, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, s, s, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view, &press);
    QApplication::sendEvent(view, &release);
    ASSERT_EQ(view->selectedIds(), std::vector<pd::EntityId>{1});
    QFile file(temp.filePath("selection.pdl"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("(with-transaction doc \"remove selection\" (foreach (id (selection doc)) (delete doc id))) "
               "(print \"deleted selection\") (eprint \"note\")");
    file.close();
    session.execute("_RUNSCRIPT " + file.fileName());
    EXPECT_FALSE(session.document().entity(1));
    EXPECT_TRUE(session.document().entity(2));
    EXPECT_TRUE(view->selectedIds().empty());
    auto* history = window.findChild<QPlainTextEdit*>();
    ASSERT_NE(history, nullptr);
    EXPECT_TRUE(history->toPlainText().contains("deleted selection"));
    EXPECT_TRUE(history->toPlainText().contains("[stderr] note"));
    session.execute("_UNDO");
    EXPECT_EQ(session.document().size(), 2u);
}

TEST(Window, ExtensionSelectionIncludesVisibleModelFlagsBeforeFirstExplicitPick) {
    pd::Document doc;
    pd::Line line({0, 0}, {4, 0});
    line.traits.isSelected = true;
    auto first = doc.add(line);
    line.traits.isVisible = false;
    doc.add(line);
    Viewport view(doc);
    EXPECT_TRUE(view.selection().ids().empty());
    EXPECT_EQ(view.selectedIds(), std::vector<pd::EntityId>{first});
}

TEST(Window, FileActionsOpenAnotherDrawingWithoutReplacingTheDirtyPage) {
    QTemporaryDir tmp;
    auto p = tmp.filePath("model.dxf");
    CommandSession session;
    Language lang;
    MainWindow window(session, lang);
    auto* view = window.findChild<Viewport*>();
    ASSERT_NE(view, nullptr);
    session.execute("_LINE 10001,10002 10003,10006");
    auto trigger = [&](const QString& name) {
        for (auto a : window.findChildren<QAction*>())
            if (a->text() == name) {
                a->trigger();
                return true;
            }
        return false;
    };
    session.setFilePicker([&](const QString&) { return p; });
    ASSERT_TRUE(trigger("Save drawing as…"));
    EXPECT_TRUE(QFile::exists(p));
    session.execute("_LINE -9,0 9,0");
    auto pt = view->camera().worldToScreen({0, 0});
    QPointF pos(pt.x, pt.y);
    QMouseEvent press(QEvent::MouseButtonPress, pos, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier),
        release(QEvent::MouseButtonRelease, pos, pos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view, &press);
    QApplication::sendEvent(view, &release);
    ASSERT_FALSE(view->selectedIds().empty());
    const auto other = tmp.filePath("copy with spaces.dxf");
    ASSERT_TRUE(QFile::copy(p, other));
    session.setFilePicker([&](const QString&) { return other; });
    ASSERT_TRUE(trigger("Open drawing…"));
    QApplication::processEvents();
    auto tabs = window.findChild<QTabWidget*>("documentTabs");
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_EQ(session.document().size(), 2u);
    EXPECT_FALSE(view->selectedIds().empty());
    EXPECT_TRUE(tabs->tabText(0).endsWith('*'));
    EXPECT_EQ(window.activeSession().document().size(), 1u);
    EXPECT_FALSE(window.activeSession().document().canUndo());
    EXPECT_TRUE(window.activeView()->selectedIds().empty());
    EXPECT_EQ(window.activeView()->camera().eye(), (pd::Vec2{10002, 10004}));
    EXPECT_TRUE(window.activeSession().execute("_SAVEAS " + p).contains("another tab"));
    EXPECT_EQ(window.activeSession().currentFile(), other);
    EXPECT_TRUE(window.openDrawing(p)); // Same file: activate, do not reload from disk.
    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(window.activeView(), view);
}

int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setOrganizationName("PlaneDraftTests");
    app.setApplicationName("Window");
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
