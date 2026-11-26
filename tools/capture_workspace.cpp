// SPDX-License-Identifier: LGPL-2.1-or-later
// Documentation tool. Drives the actual workbench; never writes the example drawings or user settings.
#include "command_session.h"
#include "language.h"
#include "layer_manager.h"
#include "main_window.h"
#include "property_inspector.h"
#include "ribbon.h"
#include "viewport.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QDockWidget>
#include <QPainter>
#include <QThread>
#include <QPlainTextEdit>
#include <utility>
#include <QProcess>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <iostream>

namespace {
// A theme switch or a page change restyles the whole window over several event-loop turns. Grabbing
// right after them catches half-relaid geometry, which used to print captions of the previous page
// as ghosts, so wait for the frame to settle instead of hoping one processEvents() is enough.
void settle() {
    for (int i = 0; i < 12; ++i) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}
bool saveFrame(MainWindow& window, const QString& path) {
    settle();
    window.repaint();
    auto image = window.grab();
    auto* view = window.activeView();
    auto frame = view->grabFramebuffer();
    if (!view->isValid() || frame.isNull())
        return false;
    // Some QPA backends grab only the QWidget backing store, not its composited GL child.
    {
        QPainter paint(&image);
        paint.drawImage(QRect(view->mapTo(&window, QPoint()), view->size()), frame);
        // The overlays are plain child widgets of the GL viewport, so window.grab() misses them for
        // exactly the same reason: draw each one that is on screen, in the order it is layered.
        for (auto* name : {"dynamicInput", "snapHint", "ucsIcon"}) {
            auto* overlay = view->findChild<QWidget*>(name);
            if (overlay && !overlay->isHidden())
                paint.drawPixmap(overlay->mapTo(&window, QPoint()), overlay->grab());
        }
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
    // The paper-space capture further down is still made with the classic toolbars, so the switch
    // stays; the model views here are photographed with the ribbon the window opens with.
    auto setRibbon = [&](bool on) {
        auto* toggle = window.findChild<QAction*>("ribbonToggle");
        if (!toggle || toggle->isChecked() == on)
            return;
        toggle->trigger();
        app.processEvents();
    };
    // The window opens with the ribbon, the way AutoCAD does: the application mark and the quick
    // access toolbar on the title strip, the tabs under them, then the panels. The first capture
    // keeps that state rather than photographing the toolbars the ribbon replaced.
    auto* ribbon = window.findChild<Ribbon*>();
    auto* dynamicInput = window.findChild<QAction*>("dynamicInputToggle");
    if (!ribbon || !dynamicInput)
        return 1;
    ribbon->setCurrentPage(0);
    command("_DIMSTYLE ESKD");
    view.setSelection(inserts);
    pointAt(view, {115.5, -18.5});
    app.processEvents();
    if (!saveFrame(window, out.filePath("workspace-dark-ru.png")))
        return 1;

    // The same window with the palettes parked: AutoCAD folds them to a strip carrying their names and
    // the drawing takes the width. This is the widest the drawing area gets.
    if (auto* parked = window.findChild<QAction*>("paletteAutoHide")) {
        parked->setChecked(true);
        app.processEvents();
        app.processEvents();
        if (!saveFrame(window, out.filePath("workspace-parked-ru.png")))
            return 1;
        parked->setChecked(false);
        app.processEvents();
    }

    // The Home page of the same ribbon with the central circle selected and the colour and line
    // weight drop-downs in view.
    view.setSelection(circles);
    pointAt(view, {-94, -43});
    app.processEvents();
    if (!saveFrame(window, out.filePath("ribbon-dark-ru.png")))
        return 1;

    // The other two tabs the gallery had never shown: the small Insert page and the View page with
    // its palettes and drafting aids.
    for (auto [page, name] : {std::pair{1, "ribbon-insert-ru.png"}, std::pair{3, "ribbon-view-ru.png"},
                              std::pair{4, "ribbon-manage-ru.png"}}) {
        ribbon->setCurrentPage(page);
        app.processEvents();
        if (!saveFrame(window, out.filePath(QString::fromLatin1(name))))
            return 1;
    }
    ribbon->setCurrentPage(0);
    app.processEvents();

    // Same window in the light English UI, ribbon on the Annotate page: a page that is populated
    // enough to show the strip, unlike the three small Insert panels.
    window.findChild<QAction*>("localeSwitch")->trigger();
    window.findChild<QAction*>("themeSwitch")->trigger();
    app.processEvents();
    console->clear();
    command("_DIMSTYLE ISO-25");
    view.setSelection(circles);
    pointAt(view, {-94, -43});
    // Visit every tab once: a page that was never shown keeps the palette and the geometry it was
    // built with, so the light theme would photograph it in the previous theme's colours.
    for (int i = 0; i < ribbon->tabs()->count(); ++i) {
        ribbon->setCurrentPage(i);
        app.processEvents();
    }
    ribbon->setCurrentPage(0);
    app.processEvents();
    ribbon->setCurrentPage(2); // the Annotate tab: dimensions and measure, three columns
    app.processEvents();
    if (!saveFrame(window, out.filePath("ribbon-light-en.png")))
        return 1;

    // The light English window on the Home page: the strip every user meets first, with the Draw,
    // Modify and Annotation panels in their own frames.
    ribbon->setCurrentPage(0);
    app.processEvents();
    if (!saveFrame(window, out.filePath("workspace-light-en.png")))
        return 1;

    // Dynamic input beside the crosshair in the dark Russian UI: F12 on, base point and a hover.
    ribbon->setCurrentPage(0);
    window.findChild<QAction*>("themeSwitch")->trigger();
    window.findChild<QAction*>("localeSwitch")->trigger();
    if (!dynamicInput->isChecked())
        dynamicInput->trigger();
    command("_LINE -60,-40");
    pointAt(view, {-24, -13});
    pointAt(view, {-22, -9});
    app.processEvents();
    if (!saveFrame(window, out.filePath("dynamic-input.png")))
        return 1;
    if (dynamicInput->isChecked())
        dynamicInput->trigger();
    view.escape();
    command("_UNDO");

    // v0.1 paper space capture: light theme, classic chrome, no docks.
    window.findChild<QAction*>("themeSwitch")->trigger();
    command("_UNDO");
    setRibbon(false);
    session.selectSpace("Панель");
    properties->hide();
    layers->hide();
    window.findChild<QDockWidget*>("CommandLine")->hide();
    app.processEvents();
    view.zoomExtents();
    if (!view.paperSpace() || session.modified() ||
        !saveFrame(window, out.filePath("layout-eskd.png")))
        return 1;

    // A real DWG produced by the bundled LibreDWG writer and opened through the reader. Dark UI,
    // docks and the transcript visible, so the import report is readable.
    auto converter = QCoreApplication::applicationDirPath() + "/tools/libredwg/dxf2dwg";
#ifdef Q_OS_WIN
    converter += ".exe";
#endif
    const auto dwg = settings.filePath("panel.dwg");
    QProcess writer;
    writer.setWorkingDirectory(settings.path());
    writer.start(converter, {"-o", dwg, QFileInfo(drawings.filePath("panel.dxf")).absoluteFilePath()});
    if (!writer.waitForFinished(120000) || writer.exitStatus() != QProcess::NormalExit ||
        writer.exitCode() != 0 || !QFileInfo::exists(dwg))
        return 1;
    window.findChild<QAction*>("themeSwitch")->trigger();
    properties->show();
    layers->show();
    window.findChild<QDockWidget*>("CommandLine")->show();
    window.findChild<QPlainTextEdit*>()->clear(); // keep only the import report in the transcript
    if (!window.openDrawing(dwg))
        return 1;
    app.processEvents();
    if (!saveFrame(window, out.filePath("dwg-open.png")))
        return 1;
    return 0;
}
