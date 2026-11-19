// SPDX-License-Identifier: LGPL-2.1-or-later
// Documentation tool. Drives the actual workbench; never writes the example drawings or user settings.
#include "command_session.h"
#include "language.h"
#include "layer_manager.h"
#include "main_window.h"
#include "property_inspector.h"
#include "viewport.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool saveFrame(MainWindow& window, const QString& path) {
    QApplication::processEvents();
    auto image = window.grab();
    auto* view = window.activeView();
    auto frame = view->grabFramebuffer();
    if (!view->isValid() || frame.isNull())
        return false;
    // Some QPA backends grab only the QWidget backing store, not its composited GL child.
    {
        QPainter paint(&image);
        paint.drawImage(QRect(view->mapTo(&window, QPoint()), view->size()), frame);
    }
    return image.save(path);
}
void pointAt(Viewport& v, pd::Vec2 point) {
    auto p = v.camera().worldToScreen(point);
    QPointF here(p.x, p.y);
    QMouseEvent move(QEvent::MouseMove, here, here, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&v, &move);
}
} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat::setDefaultFormat(Viewport::requestedFormat());
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3) {
        std::cerr << "Usage: pd_capture_workspace DRAWINGS_DIR IMAGE_DIR\n";
        return 2;
    }
    QDir drawings(args[1]), out(args[2]);
    QTemporaryDir settings;
    if (!settings.isValid() || !out.mkpath("."))
        return 1;
    app.setOrganizationName("PlaneDraftDocs");
    app.setApplicationName("WorkspaceCapture");
    app.setStyle("Fusion");
    app.setFont(QFont("DejaVu Sans", 10));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings().setValue("ui/dark", true);
    Language language;
    if (!language.setRussian(true))
        return 1;
    CommandSession first;
    MainWindow window(first, language);
    if (!window.openDrawing(drawings.filePath("cover.dxf")) ||
        !window.openDrawing(drawings.filePath("panel.dxf")))
        return 1;
    auto properties = window.findChild<PropertyInspector*>();
    auto layers = window.findChild<LayerManager*>();
    window.removeDockWidget(properties);
    window.removeDockWidget(layers);
    window.addDockWidget(Qt::RightDockWidgetArea, properties);
    window.addDockWidget(Qt::RightDockWidgetArea, layers, Qt::Vertical);
    properties->show();
    layers->show();
    window.resize(1520, 880);
    window.resizeDocks({properties}, {582}, Qt::Horizontal);
    window.resizeDocks({properties, layers}, {335, 225}, Qt::Vertical);
    window.show();
    app.processEvents();
    auto& session = window.activeSession();
    auto& model = session.document();
    auto& view = *window.activeView();
    auto command = [&](const QString& text) {
        auto input = window.findChild<QLineEdit*>("commandInput");
        input->setText(text);
        input->returnPressed();
    };
    // Use light drawing inks on the dark model background. Undo before capturing the printed sheet.
    model.transact([&](pd::Transaction& tx) {
        for (auto id : model.layerIds()) {
            if (id == pd::DefaultLayer)
                continue;
            auto layer = *model.layer(id);
            layer.color = layer.name == "Размеры" ? pd::Color{105, 197, 219} : pd::Color{190, 206, 213};
            layer.isLocked = layer.name == "Сечение";
            tx.updateLayer(id, layer);
        }
    });
    view.zoomExtents();
    pd::SelectionManager::Ids inserts, circles;
    for (auto id : model.entityIds()) {
        if (dynamic_cast<const pd::BlockReference*>(model.entity(id)))
            inserts.insert(id);
        if (dynamic_cast<const pd::Circle*>(model.entity(id)))
            circles.insert(id);
    }
    auto console = window.findChild<QPlainTextEdit*>();
    console->clear(); // Don't include build-machine paths in documentation.
    command("_DIMSTYLE ESKD");
    view.setSelection(inserts);
    pointAt(view, {115.5, -18.5});
    if (!saveFrame(window, out.filePath("workspace-dark-ru.png")))
        return 1;

    window.findChild<QAction*>("localeSwitch")->trigger();
    window.findChild<QAction*>("themeSwitch")->trigger();
    app.processEvents();
    console->clear();
    command("_DIMSTYLE ISO-25");
    view.setSelection(circles);
    pointAt(view, {-94, -43});
    if (language.russian() || !saveFrame(window, out.filePath("workspace-light-en.png")))
        return 1;

    window.findChild<QAction*>("localeSwitch")->trigger();
    app.processEvents();
    command("_UNDO");
    session.selectSpace("Панель");
    properties->hide();
    layers->hide();
    window.findChild<QDockWidget*>("CommandLine")->hide();
    app.processEvents();
    view.zoomExtents();
    return view.paperSpace() && !session.modified() && saveFrame(window, out.filePath("layout-eskd.png")) ? 0
                                                                                                          : 1;
}
