// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include "language.h"
#include "main_window.h"
#include "viewport.h"

#include <QApplication>
#include <QLocale>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QSettings>
#include <QStandardPaths>
#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    QSurfaceFormat::setDefaultFormat(Viewport::requestedFormat());
    QCoreApplication::setAttribute(
        Qt::AA_DontUseNativeDialogs); // Follow our live language, not the OS language.
    QApplication app(argc, argv);
    app.setOrganizationName("PlaneDraft");
    app.setApplicationName("PlaneDraft");
    app.setApplicationVersion(QStringLiteral(PD_VERSION));
    if (app.arguments().contains("--version")) {
        std::puts("PlaneDraft " PD_VERSION);
        return EXIT_SUCCESS;
    }
    app.setStyle("Fusion");
    Language language;
    const auto locale = QSettings().value("ui/language", QLocale::system().name().section('_', 0, 0));
    if (!language.setRussian(locale.toString() == "ru")) {
        QMessageBox::warning(nullptr, "PlaneDraft", QObject::tr("Could not load the translation catalog."));
    }
    // A QOpenGLWidget whose context cannot be created may never receive initializeGL().
    {
        QOpenGLContext probe;
        probe.setFormat(Viewport::requestedFormat());
        if (!probe.create() || probe.isOpenGLES() || probe.format().version() < qMakePair(4, 5) ||
            probe.format().profile() != QSurfaceFormat::CoreProfile) {
            QMessageBox::critical(nullptr, "PlaneDraft",
                                  QObject::tr("A desktop OpenGL 4.5 Core Profile driver is required."));
            return EXIT_FAILURE;
        }
    }
    CommandSession session;
    session.loadAliases(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/aliases.ini");
    if (app.arguments().contains("--viewport-demo")) {
        session.addViewportExample();
    }
    MainWindow window(session, language, app.arguments().contains("--load-plugins"));
    for (auto path : app.arguments().mid(1))
        if (!path.startsWith("--"))
            window.openDrawing(path);
    window.show();
    return app.exec();
}
