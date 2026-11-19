// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include "language.h"
#include "layer_manager.h"
#include "main_window.h"
#include "pointer_input.hpp"
#include "property_inspector.h"
#include <QAbstractItemDelegate>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <gtest/gtest.h>
namespace {
using namespace pd;
class Workspace : public testing::Test {
  protected:
    // Per-test settings, including tests requesting a different palette / snap mask.
    Workspace() {
        QSettings().clear();
        window = std::make_unique<MainWindow>(s, lang);
    }
    ~Workspace() override {
        window.reset();
        QSettings().clear();
    }
    CommandSession s;
    Language lang;
    std::unique_ptr<MainWindow> window;
    template <class T> T* child(const char* name) { return window->findChild<T*>(name); }
    QTableWidgetItem* field(const char* key) {
        auto table = child<QTableWidget>("propertyTable");
        for (int r = 0; r < table->rowCount(); ++r)
            if (table->item(r, 1)->data(Qt::UserRole) == key)
                return table->item(r, 1);
        return nullptr;
    }
    void edit(const char* key, QString value) {
        auto item = field(key);
        ASSERT_NE(item, nullptr) << key;
        ASSERT_TRUE(item->flags().testFlag(Qt::ItemIsEditable));
        item->setText(value);
        QApplication::processEvents();
    }
};
TEST_F(Workspace, MixedSelectionPublishesOneTransactionAndReindexesBothCurvesImmediately) {
    auto& d = s.document();
    auto circle = d.add(Circle({10, 0}, 2)), arc = d.add(Arc(CircularArc({-10, 0}, 4, 0, 1.5)));
    auto& v = *window->activeView();
    v.setSelection({circle, arc});
    ASSERT_TRUE(field("radius"));
    EXPECT_EQ(field("radius")->text(), "*VARIES*");
    EXPECT_FALSE(field("sweep"));
    int packets = 0;
    QList<Change> last;
    auto connection = QObject::connect(&s, &CommandSession::documentChanged, window.get(), [&](auto changes) {
        ++packets;
        last = changes;
    });
    auto rev = d.revision();
    BoundingBox2D area({15.8, -.1}, {16.1, .1});
    EXPECT_TRUE(d.query(area).empty());
    field("radius")->setText(
        "6"); // Before processing events: both geometry and spatial index have been published.
    EXPECT_EQ(d.revision(), rev + 1);
    EXPECT_EQ(packets, 1);
    EXPECT_EQ(last.size(), 2);
    EXPECT_EQ(dynamic_cast<const Circle&>(*d.entity(circle)).radius(), 6);
    EXPECT_EQ(dynamic_cast<const Arc&>(*d.entity(arc)).curve().radius(), 6);
    EXPECT_EQ(d.query(area), std::vector<EntityId>{circle});
    s.execute("_UNDO");
    EXPECT_EQ(d.revision(), rev + 2);
    EXPECT_TRUE(d.query(area).empty());
    ASSERT_TRUE(field("radius"));
    EXPECT_EQ(field("radius")->text(), "*VARIES*");
    EXPECT_EQ(v.selectedIds().size(), 2u);
    auto before = d.stateId();
    edit("radius", "-1");
    EXPECT_EQ(d.stateId(), before);
    EXPECT_TRUE(d.canRedo());
    s.execute("_REDO");
    auto l = d.add(Line({0, 0}, {1, 1}));
    v.setSelection({l, arc, circle});
    EXPECT_FALSE(field("radius"));
    auto props = child<QTableWidget>("propertyTable");
    EXPECT_EQ(props->rowCount(), 3);
    edit("color", "#247EAB");
    for (auto id : v.selectedIds())
        EXPECT_EQ(std::get<Color>(d.entity(id)->traits.color), (Color{36, 126, 171}));
    edit("color", "ByBlock");
    edit("weight", "0.35");
    for (auto id : v.selectedIds()) {
        EXPECT_TRUE(std::holds_alternative<ByBlock>(d.entity(id)->traits.color));
        EXPECT_DOUBLE_EQ(std::get<double>(d.entity(id)->traits.lineWeight), .35);
    }
    QObject::disconnect(connection);
}
TEST_F(Workspace, LayerChoiceDelegateAndInvalidSecondEntityAreAtomic) {
    auto& d = s.document();
    LayerId layer;
    d.transact([&](Transaction& tx) { layer = tx.addLayer(Layer("Detail")); });
    auto a = d.add(Line({0, 0}, {2, 0})), b = d.add(Line({5, 0}, {7, 0}));
    window->activeView()->setSelection({a, b});
    auto table = child<QTableWidget>("propertyTable");
    auto prop = field("layer");
    ASSERT_TRUE(prop);
    table->setCurrentItem(prop);
    table->editItem(prop);
    auto combo = table->findChild<QComboBox*>();
    ASSERT_TRUE(combo);
    combo->setCurrentText("Detail");
    auto* delegate = table->itemDelegateForColumn(1);
    delegate->commitData(combo);
    delegate->closeEditor(combo);
    QApplication::processEvents();
    EXPECT_EQ(d.entity(a)->traits.layer, layer);
    EXPECT_EQ(d.entity(b)->traits.layer, layer);
    s.execute("_UNDO");
    EXPECT_EQ(d.entity(a)->traits.layer, DefaultLayer);
    EXPECT_EQ(d.entity(b)->traits.layer, DefaultLayer);
    auto rev = d.revision();
    edit("end", "5,0"); // Valid for a; b would collapse. Do not publish a or discard redo.
    EXPECT_EQ(d.revision(), rev);
    EXPECT_EQ(dynamic_cast<const Line&>(*d.entity(a)).end(), (Vec2{2, 0}));
    EXPECT_TRUE(d.canRedo());
}
TEST_F(Workspace, TypeSpecificPropertiesPreserveIdentityAndOtherTraits) {
    auto& d = s.document();
    auto& v = *window->activeView();
    auto line = d.add(Line({0, 0}, {5, 0}));
    v.setSelection({line});
    edit("lineAngle", "90");
    auto end = dynamic_cast<const Line&>(*d.entity(line)).end();
    EXPECT_NEAR(end.x, 0, EPSILON);
    EXPECT_NEAR(end.y, 5, EPSILON);
    auto text = d.add(Text("A", {{0, 0}, {2, 3}}, Matrix3x3::translation({3, 7})));
    v.setSelection({text});
    edit("text", "Панель");
    EXPECT_EQ(dynamic_cast<const Text&>(*d.entity(text)).content(), "Панель");
    edit("insertion", "20,30");
    EXPECT_EQ(dynamic_cast<const Text&>(*d.entity(text)).placement().applyPoint({}), (Vec2{20, 30}));
    auto dim = d.add(Dimension(DimKind::Aligned, {{0, 0}, {12, 0}, {0, 5}}));
    v.setSelection({dim});
    edit("style", "ESKD");
    EXPECT_EQ(dynamic_cast<const Dimension&>(*d.entity(dim)).style(), DimStyle::eskd());
    EntityId ref;
    d.transact([&](Transaction& t) {
        const Entity* members[] = {t.entity(line)};
        auto def = t.defineBlock("sample", members);
        ref = t.add(BlockReference(def, {3, 4}));
    });
    v.setSelection({ref});
    edit("sx", "2");
    edit("sy", "-3");
    auto m = dynamic_cast<const BlockReference&>(*d.entity(ref)).placement();
    EXPECT_EQ(m.applyPoint({1, 1}), (Vec2{5, 1}));
    EXPECT_EQ(m.applyPoint({}), (Vec2{3, 4}));
}
TEST_F(Workspace, LayerTableControlsCurrentTraitsAndLocksButNotObjectSnaps) {
    auto table = child<QTableWidget>("layerTable");
    auto add = child<QPushButton>("layerAdd"), remove = child<QPushButton>("layerRemove");
    ASSERT_TRUE(table && add && remove);
    add->click();
    ASSERT_EQ(table->rowCount(), 2);
    auto name = table->item(1, 1);
    table->closePersistentEditor(name);
    name->setText("Datum");
    QApplication::processEvents();
    auto layer = table->item(1, 1)->data(Qt::UserRole).toULongLong();
    auto& d = s.document();
    ASSERT_TRUE(d.layer(layer));
    EXPECT_EQ(d.layer(layer)->name, "Datum");
    table->item(1, 0)->setCheckState(Qt::Checked);
    EXPECT_EQ(s.editor().activeLayer(), layer);
    s.execute("_LINE 0,0 20,0");
    auto id = d.entityIds().front();
    EXPECT_EQ(d.entity(id)->traits.layer, layer);
    table->item(1, 4)->setCheckState(Qt::Checked);
    auto& v = *window->activeView();
    v.zoomExtents();
    v.setSelection({id});
    input_test::move(v, {0, 0});
    ASSERT_TRUE(v.snapHit());
    EXPECT_EQ(v.snapHit()->type, SnapType::Endpoint);
    EXPECT_TRUE(d.visible(*d.entity(id)));
    EXPECT_FALSE(field("end")->flags().testFlag(Qt::ItemIsEditable));
    auto rev = d.revision();
    for (auto cmd : {"_MOVE", "_ERASE"}) {
        EXPECT_TRUE(s.execute(cmd).contains("locked"));
        s.editor().cancel();
    }
    input_test::press(v, {20, 0});
    EXPECT_EQ(s.editor().hotId(), 0u);
    input_test::release(v, {20, 2});
    table->selectRow(1);
    remove->click();
    EXPECT_EQ(d.revision(), rev);
    EXPECT_EQ(d.size(), 1u);
    ASSERT_NE(d.layer(layer), nullptr);
    for (int c : {2, 3}) { // Off and Frozen each remove snap candidates; neither destroys geometry.
        auto off = c == 2 ? Qt::Unchecked : Qt::Checked;
        table->item(1, c)->setCheckState(off);
        input_test::move(v, {0, 0});
        EXPECT_FALSE(v.snapHit());
        s.execute("_UNDO");
        ASSERT_TRUE(v.snapHit());
    }
    table->item(1, 4)->setCheckState(Qt::Unchecked);
    v.setSelection({id});
    s.execute("_ERASE");
    ASSERT_EQ(d.size(), 0u);
    table->selectRow(1);
    remove->click();
    EXPECT_FALSE(d.layer(layer));
    EXPECT_EQ(s.editor().activeLayer(), DefaultLayer);
    s.execute("_UNDO");
    EXPECT_NE(d.layer(layer), nullptr);
}
TEST_F(Workspace, LanguageAndPaletteUpdateExistingDocksActionsAndSnapControls) {
    auto& d = s.document();
    auto a = d.add(Circle({}, 1)), b = d.add(Circle({10, 0}, 3));
    auto& v = *window->activeView();
    v.setSelection({a, b});
    auto pi = child<PropertyInspector>("PropertyInspector");
    auto layers = child<LayerManager>("LayerManager");
    auto bar = child<QToolBar>("Toolbar.Modify");
    auto snaps = child<QMenu>("snapMenu");
    auto osnap = child<QAction>("osnapMode");
    ASSERT_TRUE(pi && layers && bar && snaps && osnap);
    auto rev = d.revision();
    auto locale = child<QAction>("localeSwitch");
    ASSERT_TRUE(locale);
    locale->trigger();
    QApplication::processEvents();
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    EXPECT_TRUE(buttons.button(QDialogButtonBox::Cancel)->text().contains("Отмен"));
    EXPECT_EQ(pi->windowTitle(), "Свойства");
    EXPECT_EQ(layers->windowTitle(), "Слои");
    EXPECT_EQ(field("radius")->text(), "*РАЗЛИЧНЫЕ*");
    EXPECT_EQ(bar->windowTitle(), "Редактирование");
    EXPECT_TRUE(window->menuBar()->actions().first()->text().contains("Файл"));
    EXPECT_EQ(osnap->text(), "ПРИВЯЗКА");
    EXPECT_EQ(osnap->shortcut(), QKeySequence(Qt::Key_F3));
    EXPECT_TRUE(osnap->toolTip().contains("Правая"));
    EXPECT_EQ(snaps->actions().front()->text(), "Конечная точка");
    locale->trigger();
    QApplication::processEvents();
    EXPECT_EQ(pi->windowTitle(), "Properties");
    EXPECT_EQ(field("radius")->text(), "*VARIES*");
    EXPECT_EQ(bar->windowTitle(), "Editing");
    EXPECT_EQ(osnap->text(), "OSNAP");
    auto draw = child<QToolBar>("Toolbar.Draw");
    auto line = draw->actions().first();
    auto image = line->icon().pixmap(24).toImage();
    ASSERT_FALSE(image.isNull());
    child<QAction>("themeSwitch")->trigger();
    EXPECT_NE(image, line->icon().pixmap(24).toImage());
    v.zoomExtents();
    input_test::move(v, {10, 0});
    ASSERT_TRUE(v.snapHit());
    EXPECT_EQ(v.snapHit()->type, SnapType::Center);
    EXPECT_TRUE(child<QLabel>("worldCoordinates")->text().contains("X 10.000"));
    osnap->trigger();
    EXPECT_FALSE(v.snapHit());
    osnap->trigger();
    for (auto act : snaps->actions())
        act->setChecked(act->data().toUInt() == unsigned(SnapType::Quadrant));
    EXPECT_FALSE(v.snapHit());
    input_test::move(v, {13, 0});
    ASSERT_TRUE(v.snapHit());
    EXPECT_EQ(v.snapHit()->type, SnapType::Quadrant);
    input_test::leave(v);
    EXPECT_TRUE(child<QLabel>("worldCoordinates")->text().contains("—"));
    EXPECT_EQ(d.revision(), rev);
}
TEST_F(Workspace, DocumentsIsolateHistorySpaceSelectionCameraAndPartiallyTypedCommands) {
    auto tabs = child<QTabWidget>("documentTabs");
    auto input = child<QLineEdit>("commandInput");
    auto cmd = [&](QString text) {
        input->setText(text);
        QMetaObject::invokeMethod(input, "returnPressed");
    };
    cmd("_LINE 2,0 8,4");
    auto& a = *window->activeView();
    a.zoomExtents();
    auto camera = a.camera();
    a.setSelection({1});
    input->setText("_MOVE");
    window->newDocument();
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_TRUE(input->text().isEmpty());
    auto& b = *window->activeView();
    auto& other = window->activeSession();
    cmd("_LINE 800,20 950,40");
    b.zoomExtents();
    auto cameraB = b.camera();
    EXPECT_NE(cameraB.eye(), camera.eye());
    EXPECT_TRUE(b.selectedIds().empty());
    cmd("_UNDO");
    EXPECT_TRUE(other.document().canRedo());
    EXPECT_FALSE(tabs->tabText(1).endsWith('*'));
    other.selectSpace("Layout1");
    EXPECT_TRUE(b.paperSpace());
    EXPECT_TRUE(tabs->tabText(0).endsWith('*'));
    tabs->setCurrentIndex(0);
    EXPECT_EQ(input->text(), "_MOVE");
    EXPECT_EQ(a.selectedIds(), std::vector<EntityId>{1});
    EXPECT_EQ(a.camera().eye(), camera.eye());
    EXPECT_EQ(a.camera().scale(), camera.scale());
    EXPECT_EQ(s.document().size(), 1u);
    EXPECT_FALSE(a.paperSpace());
    EXPECT_FALSE(s.document().canRedo());
    EXPECT_FALSE(window->findChild<QPlainTextEdit*>()->toPlainText().contains("800,20"));
    tabs->setCurrentIndex(1);
    EXPECT_TRUE(b.paperSpace());
    other.selectSpace({});
    EXPECT_EQ(b.camera().eye(), cameraB.eye());
    cmd("_REDO");
    EXPECT_TRUE(tabs->tabText(1).endsWith('*'));
    EXPECT_TRUE(a.selection().ids().contains(1));
}
void answer(QMessageBox::StandardButton button) {
    QTimer::singleShot(0, qApp, [button] {
        auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_TRUE(box);
        box->button(button)->click();
    });
}
TEST_F(Workspace, ClosePromptsRespectCancelSaveFailureAndTheExactSavePoint) {
    QTemporaryDir dir;
    auto file = dir.filePath("saved.dxf");
    auto tabs = child<QTabWidget>("documentTabs");
    s.execute("_LINE 0,0 3,4");
    s.execute("_SAVEAS " + file);
    ASSERT_FALSE(s.modified());
    s.execute("_LINE 10,0 20,4");
    s.execute("_UNDO");
    EXPECT_FALSE(s.modified());
    EXPECT_FALSE(tabs->tabText(0).endsWith('*'));
    s.execute("_REDO");
    ASSERT_TRUE(s.modified());
    window->newDocument();
    answer(QMessageBox::Cancel);
    EXPECT_FALSE(window->closeDocument(0));
    EXPECT_EQ(tabs->count(), 2);
    answer(QMessageBox::Save);
    EXPECT_TRUE(window->closeDocument(0));
    EXPECT_EQ(tabs->count(), 1);
    CommandSession check;
    check.execute("_OPEN " + file);
    EXPECT_EQ(check.document().size(), 2u);
    auto& remaining = window->activeSession();
    remaining.execute("_LINE 0,4 2,4");
    remaining.setFilePicker([](const auto&) { return QString{}; });
    answer(QMessageBox::Save);
    EXPECT_FALSE(window->close());
    EXPECT_TRUE(remaining.modified());
    answer(QMessageBox::Discard);
    EXPECT_TRUE(window->close());
}
TEST_F(Workspace, DockLayoutIsRestoredFromSettingsAfterCleanClose) {
    auto layers = child<LayerManager>("LayerManager");
    auto tools = child<QToolBar>("Toolbar.Annotate");
    window->addDockWidget(Qt::LeftDockWidgetArea, layers);
    tools->hide();
    EXPECT_TRUE(window->close());
    window.reset();
    window = std::make_unique<MainWindow>(s, lang);
    EXPECT_EQ(window->dockWidgetArea(child<LayerManager>("LayerManager")), Qt::LeftDockWidgetArea);
    EXPECT_TRUE(child<QToolBar>("Toolbar.Annotate")->isHidden());
}
} // namespace
