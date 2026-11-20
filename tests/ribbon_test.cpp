// SPDX-License-Identifier: LGPL-2.1-or-later
// Ribbon, quick access toolbar, context menu and the AutoCAD-style keyboard/mouse ergonomics.
#include "command_session.h"
#include "language.h"
#include "main_window.h"
#include "ribbon.h"
#include "viewport.h"
#include <iostream>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <gtest/gtest.h>

namespace {
class RibbonWorkspace : public testing::Test {
  protected:
    RibbonWorkspace() {
        QSettings().clear();
        window = std::make_unique<MainWindow>(s, lang);
    }
    ~RibbonWorkspace() override {
        window.reset();
        QSettings().clear();
    }
    template <class T> T* child(const char* name) { return window->findChild<T*>(name); }
    // Send a real key event (the vcpkg Qt build ships no Qt Test module).
    void press(QWidget* widget, int key, const QString& text = QString{}) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
        QApplication::sendEvent(widget, &event);
    }
    void type(const QString& text) {
        auto* input = child<QLineEdit>("commandInput");
        input->setText(text);
        input->returnPressed();
    }
    CommandSession s;
    Language lang;
    std::unique_ptr<MainWindow> window;
};

TEST_F(RibbonWorkspace, TabsPanelsQuickAccessAndStyleCombosExist) {
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    EXPECT_EQ(ribbon->tabs()->count(), 5);
    EXPECT_EQ(ribbon->tabs()->tabText(0), QStringLiteral("Home"));
    EXPECT_EQ(ribbon->tabs()->tabText(1), QStringLiteral("Insert"));
    EXPECT_EQ(ribbon->tabs()->tabText(2), QStringLiteral("Annotate"));
    EXPECT_EQ(ribbon->tabs()->tabText(3), QStringLiteral("View"));
    EXPECT_EQ(ribbon->tabs()->tabText(4), QStringLiteral("Manage"));
    auto* qat = ribbon->quickAccess();
    ASSERT_NE(qat, nullptr);
    QStringList quick;
    for (auto* action : qat->actions())
        if (!action->isSeparator())
            quick << action->text();
    EXPECT_EQ(quick.size(), 6);
    EXPECT_TRUE(quick.join("|").contains("Undo"));
    EXPECT_TRUE(quick.join("|").contains("Redo"));
    EXPECT_NE(ribbon->layerBox(), nullptr);
    EXPECT_NE(ribbon->colorBox(), nullptr);
    EXPECT_NE(ribbon->weightBox(), nullptr);
    // The palette and weights are the classic AutoCAD defaults, not free-form entries.
    EXPECT_EQ(ribbon->colorBox()->itemText(0), QStringLiteral("By layer"));
    EXPECT_EQ(ribbon->weightBox()->itemText(1), QStringLiteral("By block"));
}

TEST_F(RibbonWorkspace, RibbonToggleSwitchesToClassicToolbarsAndRemembersIt) {
    auto* toggle = child<QAction>("ribbonToggle");
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(ribbon, nullptr);
    ASSERT_TRUE(toggle->isChecked());
    QStringList toolbars;
    for (auto* bar : window->findChildren<QToolBar*>())
        if (bar->objectName().startsWith("Toolbar."))
            toolbars << bar->objectName();
    ASSERT_EQ(toolbars.size(), 5);
    for (auto& name : toolbars)
        EXPECT_FALSE(child<QToolBar>(name.toUtf8().constData())->isVisible());
    toggle->trigger();
    EXPECT_FALSE(toggle->isChecked());
    EXPECT_TRUE(ribbon->isHidden());
    for (auto& name : toolbars) {
        auto* bar = child<QToolBar>(name.toUtf8().constData());
        EXPECT_FALSE(bar->isHidden()) << name.toStdString();
    }
    EXPECT_FALSE(QSettings().value("ui/ribbon", true).toBool());
    toggle->trigger();
    EXPECT_TRUE(toggle->isChecked());
    EXPECT_FALSE(ribbon->isHidden());
}

TEST_F(RibbonWorkspace, LayerComboTracksTheDocumentAndAppliesToTheSelection) {
    type("_LINE 0,0 10,0");
    auto& d = s.document();
    pd::Layer ink("Ink");
    pd::LayerId lid = 0;
    d.transact([&](pd::Transaction& tx) { lid = tx.addLayer(ink); });
    auto* ribbon = window->findChild<Ribbon*>();
    auto* layers = ribbon->layerBox();
    EXPECT_GE(layers->findText("Ink"), 0);
    auto* view = window->activeView();
    view->setSelection({1});
    layers->setCurrentIndex(layers->findText("Ink"));
    emit layers->activated(layers->findText("Ink"));
    EXPECT_EQ(d.entity(1)->traits.layer, lid);
    EXPECT_TRUE(d.undo());
    EXPECT_EQ(d.entity(1)->traits.layer, pd::DefaultLayer);
}

TEST_F(RibbonWorkspace, SpaceConfirmsTheStepAndRepeatsTheLastCommand) {
    auto* input = child<QLineEdit>("commandInput");
    type("_LINE 0,0 10,0");
    ASSERT_EQ(s.document().size(), 1u);
    ASSERT_FALSE(s.editor().active());
    input->setText(QString{});
    press(input, Qt::Key_Space, QStringLiteral(" "));
    EXPECT_TRUE(s.editor().active()); // repeats LINE and asks for the first point
    EXPECT_EQ(s.document().size(), 1u);
    input->setText("0,10");
    press(input, Qt::Key_Space, QStringLiteral(" ")); // Space confirms "0,10"
    EXPECT_TRUE(s.editor().active());
    input->setText("10,10");
    press(input, Qt::Key_Space, QStringLiteral(" "));
    EXPECT_FALSE(s.editor().active());
    EXPECT_EQ(s.document().size(), 2u);
    // Free-text steps keep the literal space; the command line still owns its own text.
    s.setSelectionProvider([] { return std::vector<pd::EntityId>{1, 2}; });
    type("_BLOCK");
    ASSERT_TRUE(s.editor().active());
    EXPECT_TRUE(s.editor().expectsFreeText());
    input->setText("Frame");
    press(input, Qt::Key_Space, QStringLiteral(" "));
    EXPECT_EQ(input->text(), QStringLiteral("Frame ")); // the space stayed literal
    input->clear();
    s.editor().cancel();
}

TEST_F(RibbonWorkspace, ContextMenuOffersRepeatRecentClipboardAndProperties) {
    type("_LINE 0,0 10,0");
    auto* menu = child<QMenu>("drawingContextMenu");
    ASSERT_NE(menu, nullptr);
    auto* copy = child<QAction>("copyClipboard");
    auto* cut = child<QAction>("cutClipboard");
    auto* paste = child<QAction>("pasteClipboard");
    auto* base = child<QAction>("copyBaseClipboard");
    auto* erase = child<QAction>("eraseSelection");
    auto* props = child<QAction>("selectionProperties");
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(cut, nullptr);
    ASSERT_NE(paste, nullptr);
    ASSERT_NE(base, nullptr);
    ASSERT_NE(erase, nullptr);
    ASSERT_NE(props, nullptr);
    EXPECT_EQ(copy->shortcut(), QKeySequence(QKeySequence::Copy));
    EXPECT_EQ(cut->shortcut(), QKeySequence(QKeySequence::Cut));
    EXPECT_EQ(paste->shortcut(), QKeySequence(QKeySequence::Paste));
    EXPECT_EQ(base->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    EXPECT_EQ(erase->shortcut(), QKeySequence(QKeySequence::Delete));
    // The drawing canvas owns the shortcuts, so a focused command line keeps text editing.
    EXPECT_EQ(copy->shortcutContext(), Qt::WidgetWithChildrenShortcut);
    auto* canvas = window->activeView();
    EXPECT_TRUE(canvas->actions().contains(copy));
    window->activeView()->setSelection({1});
    copy->trigger();
    EXPECT_EQ(s.clipboardSize(), 1u);
    paste->trigger();
    ASSERT_EQ(s.document().size(), 2u);
    cut->trigger();
    EXPECT_EQ(s.document().size(), 1u);
    EXPECT_TRUE(s.document().undo());
    // Repeat and recent entries are refreshed on demand and never run in the background.
    auto* repeat = child<QAction>("contextRepeat");
    ASSERT_NE(repeat, nullptr);
    type("_LINE 0,0 10,0");
    repeat->trigger();
    EXPECT_TRUE(s.editor().active());
    s.editor().cancel();
    EXPECT_FALSE(s.recentCommands().isEmpty());
}

TEST_F(RibbonWorkspace, DynamicInputTogglesWithF12AndShowsLengthAndAngle) {
    auto* toggle = child<QAction>("dynamicInputToggle");
    ASSERT_NE(toggle, nullptr);
    EXPECT_EQ(toggle->shortcut(), QKeySequence(Qt::Key_F12));
    auto* view = window->activeView();
    EXPECT_TRUE(view->dynamicInput());
    toggle->trigger();
    EXPECT_FALSE(view->dynamicInput());
    EXPECT_FALSE(QSettings().value("view/dynamicInput", true).toBool());
    toggle->trigger();
    EXPECT_TRUE(view->dynamicInput());
    // With a base point and the cursor over the drawing the overlay reads L and ∠.
    type("_LINE 0,0");
    ASSERT_TRUE(s.editor().active());
    QMouseEvent move(QEvent::MouseMove, QPointF(400, 300), QPointF(400, 300), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view, &move);
    QApplication::processEvents();
    const auto text = view->dynamicInputText();
    EXPECT_TRUE(text.contains("L")) << text.toStdString();
    EXPECT_TRUE(text.contains("∠")) << text.toStdString();
}

// A ribbon is read by label, not by tooltip: no button may ask for less than its own caption, so
// the layout never has to fall back to "От...".
TEST_F(RibbonWorkspace, RibbonLabelsFitTheirButtons) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    QStringList clipped;
    for (auto* button : ribbon->findChildren<QToolButton*>()) {
        const auto text = button->text();
        // The quick access toolbar shows icons only; its caption lives in the tooltip.
        if (text.isEmpty() || button->isHidden() ||
            button->toolButtonStyle() == Qt::ToolButtonIconOnly)
            continue;
        const int needed = button->fontMetrics().horizontalAdvance(text);
        // The hint is what Qt needs for the full caption, margins included; a maximum below it is
        // exactly what used to squeeze "Полилиния" into "Полил...".
        const int hint = button->sizeHint().width();
        if (button->minimumWidth() < needed || button->maximumWidth() < hint)
            clipped << QStringLiteral("%1: %2/%3 < %4/%5")
                           .arg(text)
                           .arg(button->minimumWidth())
                           .arg(button->maximumWidth())
                           .arg(needed)
                           .arg(hint);
    }
    for (const auto& item : clipped)
        std::cerr << "clipped caption " << item.toStdString() << '\n';
    EXPECT_TRUE(clipped.isEmpty());
    // A narrow window scrolls the page rather than squeezing the captions.
    const auto pages = ribbon->findChildren<QScrollArea*>("ribbonScroll");
    ASSERT_FALSE(pages.isEmpty());
    for (auto* scroll : pages) {
        EXPECT_EQ(scroll->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        // The strip is as tall as its tallest page: the third row of Modify and the panel captions
        // underneath it stay inside the ribbon instead of being cut by a fixed height.
        auto* content = scroll->widget();
        ASSERT_NE(content, nullptr);
        const int needed = content->sizeHint().height();
        EXPECT_GE(scroll->height(), needed)
            << "clipped page: " << scroll->height() << " < " << needed;
    }
}

TEST_F(RibbonWorkspace, MiddleDoubleClickZoomsToExtents) {
    type("_LINE 5000,5000 5010,5000");
    auto* view = window->activeView();
    auto before = view->camera().eye();
    QMouseEvent press(QEvent::MouseButtonDblClick, QPointF(120, 90), QPointF(120, 90),
                      Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(view, &press);
    EXPECT_NE(view->camera().eye().x, before.x);
    EXPECT_NE(view->camera().scale(), 3.0);
}
} // namespace
