// SPDX-License-Identifier: LGPL-2.1-or-later
// Ribbon, quick access toolbar, context menu and the AutoCAD-style keyboard/mouse ergonomics.
#include "command_session.h"
#include "language.h"
#include "main_window.h"
#include "ribbon.h"
#include "viewport.h"
#include <algorithm>
#include <iostream>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
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
    // Panel titles are translated from the source each label remembers, so inserting a panel cannot
    // shift the title of its neighbour (the Insert tab once read "Dimensions" above Clipboard).
    auto* stack = ribbon->findChild<QStackedWidget*>("ribbonPages");
    ASSERT_NE(stack, nullptr);
    auto* insert = qobject_cast<QScrollArea*>(stack->widget(1)); // tab 1 is Insert
    ASSERT_NE(insert, nullptr);
    QStringList insertTitles;
    for (auto* label : insert->findChildren<QLabel*>("ribbonPanelTitle"))
        insertTitles << label->property("titleSource").toString();
    EXPECT_EQ(insertTitles, QStringList({"Block", "Clipboard", "Import and scripts"}));
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
    EXPECT_TRUE(s.editor().active()); // LINE keeps drawing segments, as it does in AutoCAD
    EXPECT_EQ(s.document().size(), 2u);
    press(input, Qt::Key_Space, QStringLiteral(" ")); // and Space on the empty prompt ends it
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

// F2 is the text window in AutoCAD: the prompt shrinks to a single line and expands back into a
// scrolling history.
TEST_F(RibbonWorkspace, CommandHistoryCollapsesAndExpandsWithItsToggle) {
    auto* history = child<QPlainTextEdit>("commandHistory");
    auto* toggle = child<QAction>("commandHistoryToggle");
    ASSERT_NE(history, nullptr);
    ASSERT_NE(toggle, nullptr);
    ASSERT_FALSE(history->isHidden());
    toggle->setChecked(false);
    EXPECT_TRUE(history->isHidden());
    toggle->setChecked(true);
    EXPECT_FALSE(history->isHidden());
}

// Ctrl+0 clean screen: ribbon, palettes and menus go away, the prompt stays, everything returns.
TEST_F(RibbonWorkspace, CleanScreenHidesAndRestoresTheChrome) {
    auto* clean = child<QAction>("cleanScreen");
    auto* ribbon = window->findChild<Ribbon*>();
    auto* bar = child<QToolBar>("Toolbar.File");
    auto* history = child<QPlainTextEdit>("commandHistory");
    ASSERT_NE(clean, nullptr);
    ASSERT_NE(ribbon, nullptr);
    ASSERT_NE(bar, nullptr);
    ASSERT_NE(history, nullptr);
    const bool barWasHidden = bar->isHidden();
    ASSERT_FALSE(ribbon->isHidden());
    clean->setChecked(true);
    EXPECT_TRUE(ribbon->isHidden());
    EXPECT_TRUE(window->menuBar()->isHidden());
    EXPECT_TRUE(bar->isHidden());
    EXPECT_FALSE(history->isHidden()); // the prompt stays reachable: typing is how a drawing is driven
    clean->setChecked(false);
    EXPECT_FALSE(ribbon->isHidden());
    EXPECT_FALSE(window->menuBar()->isHidden());
    EXPECT_EQ(bar->isHidden(), barWasHidden);
}

// The palette keys AutoCAD habits rely on, in one place, so a new docking area cannot quietly take
// one of them away.
TEST_F(RibbonWorkspace, PalettesAnswerToTheAutoCadPaletteKeys) {
    QStringList bound;
    for (auto* action : window->findChildren<QAction*>())
        for (const auto& key : action->shortcuts())
            bound << key.toString();
    for (const auto* key : {"Ctrl+0", "Ctrl+1", "Ctrl+2", "Ctrl+9", "F2", "F12"})
        EXPECT_TRUE(bound.contains(QString::fromLatin1(key))) << key;
}

// Up and Down walk what was typed, the way the AutoCAD command line does, and a draft typed before
// the first Up comes back when the walk returns past the newest entry.
TEST_F(RibbonWorkspace, CommandLineWalksTypedHistoryWithUpAndDown) {
    auto* input = child<QLineEdit>("commandInput");
    type("_LINE 0,0 10,0");
    type("_CIRCLE 5,5 2");
    input->clear(); // the prompt is empty again once a command has run
    press(input, Qt::Key_Up);
    EXPECT_EQ(input->text(), QStringLiteral("_CIRCLE 5,5 2"));
    press(input, Qt::Key_Up);
    EXPECT_EQ(input->text(), QStringLiteral("_LINE 0,0 10,0"));
    press(input, Qt::Key_Down);
    EXPECT_EQ(input->text(), QStringLiteral("_CIRCLE 5,5 2"));
    press(input, Qt::Key_Down);
    EXPECT_EQ(input->text(), QString{});
    input->setText(QStringLiteral("_ZO"));
    press(input, Qt::Key_Up);
    EXPECT_EQ(input->text(), QStringLiteral("_CIRCLE 5,5 2"));
    press(input, Qt::Key_Down);
    EXPECT_EQ(input->text(), QStringLiteral("_ZO"));
    input->clear();
}

// The overlay label belongs to the viewport, not to the last resize: recreating it made
// findChild("dynamicInput") return one of several stale labels.
TEST_F(RibbonWorkspace, DynamicInputLabelIsCreatedOncePerViewport) {
    window->show();
    auto* view = window->activeView();
    ASSERT_NE(view->findChild<QLabel*>("dynamicInput"), nullptr);
    window->resize(1200, 800);
    view->zoomExtents();
    QApplication::processEvents();
    EXPECT_EQ(view->findChildren<QLabel*>("dynamicInput").size(), 1);
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
        // Ask the painter's own question: would the style elide this caption in the space the
        // button reserves? That is the difference between "Полилиния" and "Полилиния…".
        const int margin = button->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, button) + 1;
        const int room = button->minimumWidth() - 2 * margin;
        const int advance = button->fontMetrics().horizontalAdvance(text);
        if (button->fontMetrics().elidedText(text, Qt::ElideRight, room) != text)
            clipped << QStringLiteral("%1: min %2 room %3 advance %4")
                           .arg(text)
                           .arg(button->minimumWidth())
                           .arg(room)
                           .arg(advance);
    }
    for (const auto& item : clipped)
        std::cerr << "clipped caption " << item.toStdString() << '\n';
    EXPECT_TRUE(clipped.isEmpty());
    // A narrow window scrolls the page rather than squeezing the captions.
    const auto pages = ribbon->findChildren<QScrollArea*>("ribbonScroll");
    ASSERT_EQ(pages.size(), 5);
    // The strip is as tall as its tallest page: the third row of Modify and the panel captions under
    // it stay inside the ribbon instead of being cut by a fixed height. Inactive pages are hidden by
    // the stack and keep no geometry, so the stack itself is what has to cover every page hint.
    auto* stack = ribbon->findChild<QStackedWidget*>("ribbonPages");
    ASSERT_NE(stack, nullptr);
    for (auto* scroll : pages) {
        EXPECT_EQ(scroll->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        ASSERT_NE(scroll->widget(), nullptr);
        EXPECT_GT(scroll->sizeHint().height(), 0);
    }
    // The strip follows the tab on screen, as AutoCAD's ribbon does, so the promise is that no page
    // is clipped while it is the visible one.
    auto* shown = qobject_cast<QScrollArea*>(stack->currentWidget());
    ASSERT_NE(shown, nullptr);
    EXPECT_GE(stack->height(), shown->sizeHint().height())
        << "clipped page: " << stack->height() << " < " << shown->sizeHint().height();
}

// AutoCAD marks the world origin with a UCS icon and parks it in the corner once the view scrolls the
// origin away, so the user always knows where X and Y point.
TEST_F(RibbonWorkspace, UcsIconMarksTheWorldOrigin) {
    window->show();
    auto* view = window->activeView();
    ASSERT_NE(view, nullptr);
    auto* icon = view->findChild<QWidget*>("ucsIcon");
    ASSERT_NE(icon, nullptr);
    EXPECT_TRUE(view->findChildren<QWidget*>("ucsIcon").size() == 1);
    view->zoomExtents();
    QApplication::processEvents();
    // The icon stays inside the drawing area whatever the camera does.
    EXPECT_TRUE(view->rect().contains(icon->geometry(), true))
        << icon->geometry().x() << ',' << icon->geometry().y() << " outside " << view->width() << 'x'
        << view->height();
}

// AutoCAD scrolls the panels with two chevrons on the tab band, not with a scroll bar under the strip:
// the strip keeps no bar of its own, the wheel and the chevrons move the page.
TEST_F(RibbonWorkspace, ChevronsScrollThePanelStrip) {
    // Narrow enough that the Home page cannot fit whatever the platform's font metrics are.
    window->resize(800, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    auto* back = ribbon->findChild<QToolButton*>("ribbonScrollLeft");
    auto* forward = ribbon->findChild<QToolButton*>("ribbonScrollRight");
    ASSERT_NE(back, nullptr);
    ASSERT_NE(forward, nullptr);
    auto* page = ribbon->findChild<QScrollArea*>("ribbonScroll");
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    auto* bar = page->horizontalScrollBar();
    ASSERT_GT(bar->maximum(), 0) << "the Home page is wider than a narrow window";
    EXPECT_FALSE(back->isEnabled()) << "the page starts at its left end";
    EXPECT_TRUE(forward->isEnabled());
    forward->click();
    EXPECT_GT(bar->value(), 0);
    EXPECT_TRUE(back->isEnabled());
    bar->setValue(bar->maximum());
    EXPECT_FALSE(forward->isEnabled());
}

// The coordinate readout owns the left end of the status bar in AutoCAD and never hides under a hint,
// which is why the hint is a widget of its own instead of a temporary status message.
TEST_F(RibbonWorkspace, CoordinatesSitAtTheLeftEndOfTheStatusBar) {
    window->show();
    QApplication::processEvents();
    auto* coords = window->findChild<QLabel*>("worldCoordinates");
    auto* message = window->findChild<QLabel*>("statusMessage");
    ASSERT_NE(coords, nullptr);
    ASSERT_NE(message, nullptr);
    EXPECT_TRUE(coords->isVisible());
    EXPECT_TRUE(message->isVisible());
    EXPECT_LT(coords->x(), message->x()) << "the readout comes before the hint";
}

// AutoCAD's ribbon folds down to its tabs on a double click, which gives the drawing the whole
// window without losing the tabs.
TEST_F(RibbonWorkspace, DoubleClickOnATabMinimizesTheRibbon) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    auto* pages = ribbon->findChild<QStackedWidget*>("ribbonPages");
    auto* tabs = ribbon->findChild<QTabBar*>("ribbonTabs");
    ASSERT_NE(pages, nullptr);
    ASSERT_NE(tabs, nullptr);
    const int tall = ribbon->height();
    ASSERT_TRUE(pages->isVisible());
    const QPoint centre = tabs->tabRect(0).center();
    QMouseEvent press(QEvent::MouseButtonDblClick, QPointF(centre), QPointF(tabs->mapToGlobal(centre)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tabs, &press);
    QApplication::processEvents();
    EXPECT_TRUE(ribbon->collapsed());
    EXPECT_FALSE(pages->isVisible());
    EXPECT_LT(ribbon->height(), tall) << "the minimized ribbon keeps its strip";
    QApplication::sendEvent(tabs, &press);
    QApplication::processEvents();
    EXPECT_FALSE(ribbon->collapsed());
    EXPECT_TRUE(pages->isVisible());
    EXPECT_EQ(ribbon->height(), tall);
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
