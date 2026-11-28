// SPDX-License-Identifier: LGPL-2.1-or-later
// Ribbon, quick access toolbar, context menu and the AutoCAD-style keyboard/mouse ergonomics.
#include "command_session.h"
#include "language.h"
#include "main_window.h"
#include "ribbon.h"
#include "viewport.h"
#include <algorithm>
#include <cmath>
#include <iostream>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QEnterEvent>
#include <QGridLayout>
#include <QImage>
#include <QPixmap>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVector>
#include <gtest/gtest.h>

namespace {
// Ask the painter's own question of every caption on the ribbon: would the style elide it in the
// space the button reserves? That is the difference between "Полилиния" and "Полилиния…".
QStringList clippedCaptions(Ribbon* ribbon) {
    QStringList clipped;
    for (auto* button : ribbon->findChildren<QToolButton*>()) {
        const auto text = button->text();
        // The quick access toolbar shows icons only; its caption lives in the tooltip.
        if (text.isEmpty() || button->isHidden() ||
            button->toolButtonStyle() == Qt::ToolButtonIconOnly)
            continue;
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
    return clipped;
}

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

// AutoCAD keeps the quick access toolbar on the title strip; here that strip is the menu row, and the
// ribbon begins with its tabs instead of a band of its own. The ribbon still owns the toolbar, so
// setQuickAccess keeps filling it.
// AutoCAD's ribbon keeps one height whatever tab is open, so switching tabs does not move the drawing
// area; the strip is as tall as the tallest page.
// AutoCAD's status bar carries a small glyph beside each drawing aid's word, so the cells read at a
// glance instead of as three similar words in a row.
TEST_F(RibbonWorkspace, DrawingAidCellsCarryAGlyph) {
    for (auto name : {"gridMode", "orthoMode", "polarMode", "osnapMode"}) {
        auto* action = window->findChild<QAction*>(name);
        ASSERT_NE(action, nullptr) << name;
        EXPECT_FALSE(action->icon().isNull()) << name;
    }
    auto* osnap = window->findChild<QAction*>("osnapMode");
    const QPixmap glyph = osnap->icon().pixmap({12, 12});
    EXPECT_FALSE(glyph.isNull());
    // The aperture is drawn, not an empty square: some pixel inside carries ink.
    const QImage image = glyph.toImage();
    bool inked = false;
    for (int x = 0; x < image.width() && !inked; ++x)
        for (int y = 0; y < image.height() && !inked; ++y)
            inked = qAlpha(image.pixel(x, y)) > 0;
    EXPECT_TRUE(inked);
}

TEST_F(RibbonWorkspace, RibbonKeepsOneHeightAcrossTabs) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    auto* stack = ribbon->findChild<QStackedWidget*>("ribbonPages");
    ASSERT_NE(stack, nullptr);
    ribbon->setCurrentPage(0);
    QApplication::processEvents();
    const int home = stack->height();
    ASSERT_GT(home, 0);
    for (int page = 1; page < ribbon->tabs()->count(); ++page) {
        ribbon->setCurrentPage(page);
        QApplication::processEvents();
        EXPECT_EQ(stack->height(), home) << "page " << page;
    }
    ribbon->setCurrentPage(0);
}

// The band is the window's, not the drawing area's: AutoCAD's tabs run from edge to edge and the
// palettes start underneath them, so a dock cannot push the ribbon inwards.
// The window is for the drawing. AutoCAD's chrome takes about a fifth of the height and its palettes
// a quarter of the width; a band that grows a row for one command, or a palette that spells its
// headings out, takes that room back from the drawing.
// A page is as tall as its buttons and its caption, and no more: the band is sized from the tallest
// page, so one greedy page costs every tab a strip of the drawing area.
// A ribbon never cuts a panel in half: a panel that no longer fits folds down to its name and its
// entries move into the menu behind it, which is how AutoCAD shrinks the strip.
TEST_F(RibbonWorkspace, TheStripFoldsAPanelInsteadOfCuttingIt) {
    window->resize(1240, 800);
    window->show();
    QApplication::processEvents();
    auto* stack = child<QStackedWidget>("ribbonPages");
    ASSERT_NE(stack, nullptr);
    auto* scroll = qobject_cast<QScrollArea*>(stack->currentWidget());
    ASSERT_NE(scroll, nullptr);
    QWidget* page = scroll->widget();
    ASSERT_NE(page, nullptr);
    auto* row = qobject_cast<QHBoxLayout*>(page->layout());
    ASSERT_NE(row, nullptr);
    EXPECT_LE(row->minimumSize().width(), scroll->viewport()->width())
        << "the strip asks for more than the window has";
    bool folded = false;
    for (auto* fold : page->findChildren<QToolButton*>(QStringLiteral("ribbonPanelFold"))) {
        if (!fold->isVisible())
            continue;
        folded = true;
        ASSERT_NE(fold->menu(), nullptr) << "a folded panel still reaches its commands";
        EXPECT_FALSE(fold->menu()->actions().isEmpty());
    }
    // The narrow window folds at least one panel, and the strip stops scrolling once it does.
    EXPECT_TRUE(folded);
    EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
}

TEST_F(RibbonWorkspace, EveryPageKeepsToTheHeightOfItsButtons) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    auto* stack = ribbon->findChild<QStackedWidget*>("ribbonPages");
    ASSERT_NE(ribbon, nullptr);
    ASSERT_NE(stack, nullptr);
    for (int page = 0; page < ribbon->tabs()->count(); ++page) {
        ribbon->setCurrentPage(page);
        QApplication::processEvents();
        const auto* scroll = qobject_cast<QScrollArea*>(stack->currentWidget());
        ASSERT_NE(scroll, nullptr) << "page " << page;
        const QWidget* content = scroll->widget();
        ASSERT_NE(content, nullptr) << "page " << page;
        // A small entry is held to one line, so the tallest page — Home with its three rows — asks
        // for a strip this short; the band used to keep a button's worth of air around every row.
        if (content->sizeHint().height() > 82)
            ADD_FAILURE() << "page " << ribbon->tabs()->tabText(page).toStdString() << " asks "
                          << content->sizeHint().height() << " px";
    }
    ribbon->setCurrentPage(0);
}

// The Home page carries six panels; a panel that is built but never laid out leaves the strip half a
// band wide, which is what a fresh window showed while the tabs were already in place.
TEST_F(RibbonWorkspace, TheHomePageLaysOutEveryPanelOnTheFirstFrame) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    auto* stack = ribbon->findChild<QStackedWidget*>("ribbonPages");
    ASSERT_NE(stack, nullptr);
    auto* scroll = qobject_cast<QScrollArea*>(stack->currentWidget());
    ASSERT_NE(scroll, nullptr);
    QWidget* page = scroll->widget();
    ASSERT_NE(page, nullptr);
    int panels = 0;
    for (auto* label : page->findChildren<QLabel*>(QStringLiteral("ribbonPanelTitle"))) {
        // A folded panel hides its caption and carries its name on a button instead; either way the
        // panel is on the page, and none of the six is left out.
        QWidget* frame = label->parentWidget();
        ASSERT_NE(frame, nullptr);
        EXPECT_TRUE(label->isVisible() || frame->findChild<QToolButton*>(QStringLiteral("ribbonPanelFold")))
            << "panel " << label->text().toStdString() << " is neither drawn nor folded";
        ++panels;
    }
    EXPECT_GE(panels, 6) << "the Home page keeps its Draw, Modify, Annotate, Layers, Blocks and Properties";
    // A strip narrower than the panels it carries clips the last of them and leaves a blank gap
    // where the next panel should start.
    EXPECT_GE(page->width(), page->layout()->minimumSize().width());
    EXPECT_EQ(scroll->horizontalScrollBar()->value(), 0) << "a fresh page starts at its first panel";
    // The drop-downs of Layers, Colour and Weight exist and are measured even when their panel is the
    // one that folded away.
    for (const char* name : {"ribbonLayer", "ribbonColor", "ribbonWeight"}) {
        auto* combo = ribbon->findChild<QComboBox*>(QString::fromLatin1(name));
        ASSERT_NE(combo, nullptr) << name;
        EXPECT_GT(combo->width(), 0) << name;
    }
}

TEST_F(RibbonWorkspace, ChromeLeavesTheDrawingAreaMostOfTheWindow) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    auto* view = window->activeView();
    ASSERT_NE(ribbon, nullptr);
    ASSERT_NE(view, nullptr);
    EXPECT_LT(ribbon->height(), window->height() / 5) << "the band stays a band";
    EXPECT_GE(view->width(), window->width() * 7 / 10) << "the palettes keep to a third";
}

// The band is its tabs and its page, nothing else: the quick access toolbar moved to the title strip
// years of code ago, but the ribbon kept its row.
// The band is the ribbon plus the frame the style draws around an item. A plain toolbar asked the
// style for a tool bar's height and came out about twenty-five pixels taller than what it carried.
// AutoCAD parks its palettes as a strip that carries their names; a click on the strip opens them and
// the drawing keeps the width in the meantime.
TEST_F(RibbonWorkspace, ParkedPalettesLeaveTheDrawingTheWholeWidth) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* autoHide = child<QAction>("paletteAutoHide");
    auto* view = window->activeView();
    auto* inspector = window->findChild<QDockWidget*>("PropertyInspector");
    ASSERT_NE(autoHide, nullptr);
    ASSERT_NE(view, nullptr);
    ASSERT_NE(inspector, nullptr);
    const int open = view->width();
    autoHide->setChecked(true);
    QApplication::processEvents();
    EXPECT_LE(inspector->width(), 40) << "a parked palette is the strip it carries";
    EXPECT_GT(view->width(), open + 150) << "the drawing takes the width the palette gave back";
    autoHide->setChecked(false);
    QApplication::processEvents();
    EXPECT_EQ(view->width(), open);
    EXPECT_GT(inspector->width(), 200);
    EXPECT_FALSE(QSettings().value("ui/palettesParked", true).toBool())
        << "the opened palettes are what the next session opens";
}

// AutoCAD's parked palettes open under the pointer; the strip is what the pointer has to reach.
// AutoCAD's parked palettes open under the pointer; the strip is what the pointer has to reach.
// The tab strip is as tall as its captions: the style's tab padding is a third taller than the words,
// and the band carries that third on every page.
TEST_F(RibbonWorkspace, TheTabStripIsAsTallAsItsCaption) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    const int strip = ribbon->tabs()->height();
    EXPECT_GT(strip, 0);
    EXPECT_LE(strip, ribbon->tabs()->fontMetrics().height() + 10)
        << "the strip is " << strip << " px for a " << ribbon->tabs()->fontMetrics().height()
        << " px caption";
}

TEST_F(RibbonWorkspace, TheStripOpensThePaletteUnderThePointer) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* autoHide = child<QAction>("paletteAutoHide");
    auto* inspector = window->findChild<QDockWidget*>("PropertyInspector");
    auto* strip = window->findChild<QWidget*>("PaletteTitle.PropertyInspector");
    ASSERT_NE(autoHide, nullptr);
    ASSERT_NE(inspector, nullptr);
    ASSERT_NE(strip, nullptr);
    autoHide->setChecked(true);
    QApplication::processEvents();
    ASSERT_LE(inspector->width(), 40);
    const QPointF centre(strip->width() / 2.0, strip->height() / 2.0);
    QEnterEvent enter(centre, centre, strip->mapToGlobal(centre));
    QApplication::sendEvent(strip, &enter);
    QApplication::processEvents();
    EXPECT_GT(inspector->width(), 200) << "the palette opens under the pointer";
}

TEST_F(RibbonWorkspace, TheBandIsNoTallerThanItsRibbon) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    auto* band = window->findChild<QToolBar*>("ribbonBar");
    ASSERT_NE(ribbon, nullptr);
    ASSERT_NE(band, nullptr);
    ASSERT_FALSE(band->isHidden());
    EXPECT_LE(band->height(), ribbon->height() + 10)
        << "the band pads the ribbon by " << band->height() - ribbon->height() << " px";
}

TEST_F(RibbonWorkspace, TheBandReservesNoRowForTheQuickAccessToolbar) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    auto* stack = ribbon->findChild<QStackedWidget*>("ribbonPages");
    ASSERT_NE(stack, nullptr);
    ribbon->setCurrentPage(0);
    QApplication::processEvents();
    EXPECT_LE(ribbon->height(), ribbon->tabs()->height() + stack->height() + 8)
        << "band " << ribbon->height() << ", tabs " << ribbon->tabs()->height() << ", page "
        << stack->height();
}

TEST_F(RibbonWorkspace, RibbonRunsEdgeToEdgeAboveThePalettes) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    auto* inspector = window->findChild<QDockWidget*>("PropertyInspector");
    ASSERT_NE(ribbon, nullptr);
    ASSERT_NE(inspector, nullptr);
    ASSERT_FALSE(inspector->isHidden());
    EXPECT_GE(ribbon->width(), window->width() - 16) << "the band spans the window";
    const QPoint bandTop = ribbon->mapTo(window.get(), QPoint(0, 0));
    const QPoint paletteTop = inspector->mapTo(window.get(), QPoint(0, 0));
    EXPECT_LE(bandTop.y() + ribbon->height(), paletteTop.y())
        << "the palettes begin under the band";
}

TEST_F(RibbonWorkspace, QuickAccessRidesOnTheMenuRow) {
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    auto* qat = ribbon->quickAccess();
    ASSERT_NE(qat, nullptr);
    EXPECT_EQ(qat->parentWidget(), window->menuBar());
    EXPECT_EQ(ribbon->findChild<QToolBar*>("quickAccess"), nullptr);
}

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
    EXPECT_EQ(insertTitles,
              QStringList({"Block", "Clipboard", "Drawings", "Export and scripts"}));
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
    // Both boxes read what the device measures from the base point, as AutoCAD's do, so a fresh
    // prompt never shows the placeholder a blank box would carry.
    auto numbers = [](const QString& line) {
        QList<double> out;
        const QRegularExpression pattern(QStringLiteral(R"(-?\d+(?:\.\d+)?)"));
        for (const auto& match : pattern.globalMatch(line))
            out << match.captured(0).toDouble();
        return out;
    };
    const auto measured = numbers(text);
    ASSERT_EQ(measured.size(), 2) << text.toStdString();
    EXPECT_LT(measured.front(), 1e6); // a number, not the ellipsis of a blank box
    EXPECT_LT(std::abs(measured.back()), 360.0);
    s.editor().cancel();
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
    const auto clipped = clippedCaptions(ribbon);
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
// the strip keeps no bar of its own, the chevrons move the page in front of the user, and they dim at
// the two ends. The page is given a width of its own, so the range is not a platform's font metrics.
TEST_F(RibbonWorkspace, ChevronsScrollThePanelStrip) {
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    auto* back = ribbon->findChild<QToolButton*>("ribbonScrollLeft");
    auto* forward = ribbon->findChild<QToolButton*>("ribbonScrollRight");
    ASSERT_NE(back, nullptr);
    ASSERT_NE(forward, nullptr);
    auto* stack = ribbon->findChild<QStackedWidget*>("ribbonPages");
    ASSERT_NE(stack, nullptr);
    auto* page = qobject_cast<QScrollArea*>(stack->currentWidget());
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    EXPECT_FALSE(page->horizontalScrollBar()->isVisible()) << "the strip shows no bar";
    page->setWidgetResizable(false);
    ASSERT_NE(page->widget(), nullptr);
    page->widget()->resize(2400, 160);
    QApplication::processEvents();
    auto* bar = page->horizontalScrollBar();
    ASSERT_GT(bar->maximum(), 0);
    bar->setValue(bar->minimum());
    EXPECT_FALSE(back->isEnabled()) << "the page starts at its left end";
    EXPECT_TRUE(forward->isEnabled());
    forward->click();
    EXPECT_GT(bar->value(), 0);
    EXPECT_TRUE(back->isEnabled());
    bar->setValue(bar->maximum());
    EXPECT_FALSE(forward->isEnabled());
}

// AutoCAD names the line it waits on: the command line opens with "Command:".
TEST_F(RibbonWorkspace, CommandLineNamesItself) {
    window->show();
    QApplication::processEvents();
    auto* prompt = window->findChild<QLabel*>("commandPrompt");
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->text(), QStringLiteral("Command:"));
    EXPECT_TRUE(prompt->isVisible());
    auto* input = window->findChild<QLineEdit*>("commandInput");
    ASSERT_NE(input, nullptr);
    EXPECT_LT(prompt->x(), input->x()) << "the name comes before what is typed";
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

// A panel with a title and nothing under it is a promise the ribbon does not keep: every panel has to
// carry at least one control.
TEST_F(RibbonWorkspace, EveryRibbonPanelCarriesAControl) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    const auto frames = ribbon->findChildren<QWidget*>("ribbonPanel");
    ASSERT_FALSE(frames.isEmpty());
    for (auto* frame : frames) {
        QString title = QStringLiteral("?");
        if (auto* label = frame->findChild<QLabel*>("ribbonPanelTitle"))
            title = label->property("titleSource").toString();
        const bool filled = !frame->findChildren<QAbstractButton*>().isEmpty() ||
                            !frame->findChildren<QComboBox*>().isEmpty();
        EXPECT_TRUE(filled) << title.toStdString();
    }
}

// A panel of small buttons reads as a grid: the entries of one column start at the same x, so the
// captions line up instead of stepping in and out with their lengths, the way AutoCAD draws them.
TEST_F(RibbonWorkspace, SmallButtonsShareOneColumnWidth) {
    // Wide enough that no panel folds: the grids of the folded ones are out of the picture.
    window->resize(2600, 880);
    window->show();
    QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    int checked = 0;
    for (auto* grid : ribbon->findChildren<QGridLayout*>()) {
        QVector<int> columnX;
        for (int i = 0; i < grid->count(); ++i) {
            auto* button = qobject_cast<QToolButton*>(grid->itemAt(i)->widget());
            // Only the page on screen has geometry; the others keep none until they are shown.
            if (!button || !button->isVisible())
                continue;
            int row = 0;
            int column = 0;
            int rowSpan = 1;
            int columnSpan = 1;
            grid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
            const int x = button->mapTo(grid->parentWidget(), QPoint(0, 0)).x();
            if (column >= columnX.size()) {
                columnX.resize(column + 1, -1);
            }
            if (columnX[column] < 0) {
                columnX[column] = x;
                ++checked;
            } else {
                EXPECT_EQ(x, columnX[column]) << "column " << column;
            }
        }
    }
    EXPECT_GT(checked, 3);
}

// AutoCAD's file tabs are flat: the current one is a patch of its own colour and no tab draws an
// outline around itself.
TEST_F(RibbonWorkspace, FileTabsAreFlat) {
    window->resize(1520, 880);
    window->show();
    QApplication::processEvents();
    auto* tabs = window->findChild<QTabWidget*>("documentTabs");
    ASSERT_NE(tabs, nullptr);
    auto* bar = tabs->tabBar();
    ASSERT_GE(bar->count(), 1);
    const QImage image = bar->grab().toImage();
    ASSERT_FALSE(image.isNull());
    const qreal ratio = image.devicePixelRatio();
    const auto at = [&](int x, int y) {
        return image.pixelColor(std::clamp(int(x * ratio), 0, image.width() - 1),
                                std::clamp(int(y * ratio), 0, image.height() - 1));
    };
    const QRect cell = bar->tabRect(bar->currentIndex());
    ASSERT_FALSE(cell.isEmpty());
    const QColor fill = at(cell.center().x(), cell.top() + 3);
    // An outline would put a different colour one pixel inside the edge.
    EXPECT_EQ(at(cell.left() + 1, cell.center().y()), fill);
    const int row = std::min(bar->width() - 2, cell.right() + 60);
    EXPECT_NE(at(row, cell.center().y()), fill);
}

// AutoCAD's file tabs carry one plain cross in the text colour; the star says that a drawing has
// unsaved edits, so the cross never changes colour for it.
TEST_F(RibbonWorkspace, DocumentTabsCarryOnePlainCross) {
    auto* tabs = window->findChild<QTabWidget*>("documentTabs");
    ASSERT_NE(tabs, nullptr);
    ASSERT_GE(tabs->count(), 1);
    auto* close = qobject_cast<QToolButton*>(tabs->tabBar()->tabButton(0, QTabBar::RightSide));
    ASSERT_NE(close, nullptr);
    EXPECT_EQ(close->text(), QStringLiteral("\u2715"));
    EXPECT_TRUE(close->icon().isNull());
    EXPECT_EQ(close->toolTip(), QStringLiteral("Close the drawing"));
    EXPECT_EQ(close->size(), QSize(16, 16));
}

// AutoCAD's shortcut menu during a command starts with the two keys that matter: Enter for the next
// step and Cancel for the way out. Both are there only while a command is running.
TEST_F(RibbonWorkspace, ContextMenuOffersEnterWhileACommandRuns) {
    auto* enter = window->findChild<QAction*>("contextEnter");
    auto* cancel = window->findChild<QAction*>("contextCancel");
    ASSERT_NE(enter, nullptr);
    ASSERT_NE(cancel, nullptr);
    EXPECT_FALSE(enter->isVisible());
    EXPECT_FALSE(cancel->isVisible());
    type("_LINE 0,0");
    QApplication::processEvents();
    EXPECT_TRUE(enter->isVisible());
    EXPECT_TRUE(cancel->isVisible());
    EXPECT_EQ(enter->text(), QStringLiteral("Enter"));
    type("");
    QApplication::processEvents();
    EXPECT_FALSE(enter->isVisible());
    window->activeView()->escape();
}

// AutoCAD names the point it is about to snap to, in the words of its object-snap menu. The label is a
// child of the viewport, created once, and stays hidden until a snap is under the crosshair.
TEST_F(RibbonWorkspace, SnapHintBelongsToTheViewport) {
    window->show();
    auto* view = window->activeView();
    ASSERT_NE(view, nullptr);
    QMouseEvent move(QEvent::MouseMove, QPointF(200, 150), QPointF(200, 150), Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    QApplication::sendEvent(view, &move);
    QApplication::processEvents();
    auto* hint = view->findChild<QLabel*>("snapHint");
    ASSERT_NE(hint, nullptr);
    EXPECT_EQ(view->findChildren<QLabel*>("snapHint").size(), 1);
}

// AutoCAD's ribbon folds down to its tabs on a double click, which gives the drawing the whole
// window without losing the tabs.
TEST_F(RibbonWorkspace, DoubleClickOnATabMinimizesTheRibbon) {
    window->resize(1520, 880);
    window->show();
    for (int i = 0; i < 3; ++i)
        QApplication::processEvents();
    auto* ribbon = window->findChild<Ribbon*>();
    ASSERT_NE(ribbon, nullptr);
    auto* pages = ribbon->findChild<QStackedWidget*>("ribbonPages");
    auto* tabs = ribbon->findChild<QTabBar*>("ribbonTabs");
    ASSERT_NE(pages, nullptr);
    ASSERT_NE(tabs, nullptr);
    ASSERT_TRUE(pages->isVisible());
    const QPoint centre = tabs->tabRect(0).center();
    QMouseEvent press(QEvent::MouseButtonDblClick, QPointF(centre), QPointF(tabs->mapToGlobal(centre)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tabs, &press);
    QApplication::processEvents();
    EXPECT_TRUE(ribbon->collapsed());
    EXPECT_FALSE(pages->isVisible());
    const int folded = ribbon->height();
    // A window that a test driver never got to lay out reports no strip at all, so the height of the
    // strip is only compared where there is one to compare with.
    if (const int strip = tabs->height(); strip > 0)
        EXPECT_LE(folded, strip + 8) << "a minimized band is the tab strip";
    QApplication::sendEvent(tabs, &press);
    QApplication::processEvents();
    EXPECT_FALSE(ribbon->collapsed());
    EXPECT_TRUE(pages->isVisible());
    EXPECT_GT(ribbon->height(), folded) << "the band comes back with the page";
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
