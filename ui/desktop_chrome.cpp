// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include "drawing_page.h"
#include "language.h"
#include "layer_manager.h"
#include "main_window.h"
#include "property_inspector.h"
#include "ribbon.h"
#include "viewport.h"
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QIconEngine>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QSvgRenderer>
#include <QToolBar>
#include <QToolButton>
#include <QComboBox>
namespace {
// Render the original SVG at the painter's actual device scale, using the current palette.
class DraftIcon final : public QIconEngine {
  public:
    explicit DraftIcon(QByteArray svg) : svg_(std::move(svg)) {}
    QIconEngine* clone() const override { return new DraftIcon(svg_); }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        auto p = qApp->palette();
        auto ink =
            p.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::WindowText);
        bool dark = p.window().color().lightness() < 128;
        auto data = svg_;
        const auto primary = ink.name().toLatin1();
        const QByteArray accent(dark ? "#77dbce" : "#00747a");
        data.replace("#233747", primary);
        data.replace("#007f81", mode == QIcon::Disabled ? primary : accent);
        QSvgRenderer renderer(data);
        renderer.render(painter, rect);
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode m, QIcon::State s) override {
        QPixmap pix(size);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        paint(&p, QRect(QPoint(), size), m, s);
        return pix;
    }

  private:
    QByteArray svg_;
};
QIcon icon(QString name) {
    QFile f(":/draft/icons/" + name + ".svg");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QIcon(new DraftIcon(f.readAll()));
}
constexpr pd::SnapType types[] = {pd::SnapType::Endpoint,     pd::SnapType::Midpoint, pd::SnapType::Center,
                                  pd::SnapType::Intersection, pd::SnapType::Nearest,  pd::SnapType::Quadrant,
                                  pd::SnapType::Insertion};
} // namespace
void MainWindow::setupChrome() {
    Q_INIT_RESOURCE(pd_icons);
    QSettings cfg;
    panels_ = view_->addMenu(QString());
    for (auto dock : {static_cast<QDockWidget*>(inspector_), static_cast<QDockWidget*>(layers_), console_})
        panels_->addAction(dock->toggleViewAction());
    panels_->addSeparator();
    for (auto name : {"File", "Draw", "Modify", "Annotate", "View"}) {
        auto bar = addToolBar(QString());
        bar->setObjectName(QString("Toolbar.") + name);
        bar->setIconSize({23, 23});
        bars_ << bar;
        panels_->addAction(bar->toggleViewAction());
    }
    auto add = [&](int b, QAction* action, const QString& name) {
        action->setIcon(icon(name));
        bars_[b]->addAction(action);
    };
    add(0, new_, "new");
    add(0, open_, "open");
    add(0, save_, "save");
    add(2, undo_, "undo");
    add(2, redo_, "redo");
    bars_[2]->addSeparator();
    const QStringList draw{"line", "pline", "circle", "arc", "rectang", "polygon", "hatch"};
    const QStringList modify{"move", "copy", "rotate", "scale", "mirror", "trim", "extend",
                             "offset", "fillet", "chamfer", "array", "matchprop", "erase", "explode"};
    const QStringList annotate{"dist", "area"};
    for (auto a : editActions_) {
        auto k = a->data().toString().toLower();
        a->setIcon(icon(k));
        if (draw.contains(k))
            add(1, a, k);
        else if (modify.contains(k))
            add(2, a, k);
        else if (k.startsWith("dim") || k == "hatch" || annotate.contains(k))
            add(3, a, k);
        else if (k == "layout")
            add(4, a, k);
    }
    cut_->setIcon(icon("cut"));
    copy_->setIcon(icon("copy"));
    copyBase_->setIcon(icon("copy"));
    paste_->setIcon(icon("paste"));
    properties_->setIcon(icon("properties"));
    erase_->setIcon(icon("erase"));
    repeat_->setIcon(icon("redo"));
    exportPdf_->setIcon(icon("pdf"));
    add(4, extents_, "extents");
    // F2 is the text window in AutoCAD and Ctrl+9 the command line; here both collapse the prompt to
    // a single line and expand it back into a scrolling history.
    historyToggle_ = view_->addAction(QString());
    historyToggle_->setObjectName("commandHistoryToggle");
    historyToggle_->setCheckable(true);
    historyToggle_->setChecked(cfg.value("view/commandHistory", true).toBool());
    historyToggle_->setShortcuts({QKeySequence(Qt::Key_F2), QKeySequence(QStringLiteral("Ctrl+9"))});
    connect(historyToggle_, &QAction::toggled, this, &MainWindow::showCommandHistory);
    history_->setVisible(historyToggle_->isChecked());
    grid_ = view_->addAction(QString());
    grid_->setObjectName("gridMode");
    grid_->setCheckable(true);
    grid_->setChecked(cfg.value("view/grid", true).toBool());
    add(4, grid_, "grid");
    grid_->setShortcut(Qt::Key_F7);
    connect(grid_, &QAction::toggled, this, [this](bool on) {
        if (canvas_)
            canvas_->setGrid(on);
        QSettings().setValue("view/grid", on);
    });
    osnap_ = settings_->addAction(QString());
    osnap_->setObjectName("osnapMode");
    osnap_->setShortcut(Qt::Key_F3);
    osnap_->setCheckable(true);
    osnap_->setChecked(cfg.value("view/osnap", true).toBool());
    snapMenu_ = settings_->addMenu(QString());
    snapMenu_->setObjectName("snapMenu");
    auto mask = cfg.value("view/snapTypes", static_cast<unsigned>(pd::SnapEngine::modes)).toUInt();
    for (auto t : types) {
        auto a = snapMenu_->addAction(QString());
        a->setCheckable(true);
        a->setData(static_cast<unsigned>(t));
        a->setChecked(mask & static_cast<unsigned>(t));
        connect(a, &QAction::toggled, this, [this] { updateSnap(); });
    }
    connect(osnap_, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("view/osnap", on);
        updateSnap();
    });
    coords_ = new QLabel(this);
    coords_->setObjectName("worldCoordinates");
    coords_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    statusBar()->addPermanentWidget(coords_);
    lang_ = new QAction(this);
    lang_->setObjectName("localeSwitch");
    theme_ = settings_->addAction(QString());
    theme_->setObjectName("themeSwitch");
    theme_->setCheckable(true);
    theme_->setChecked(cfg.value("ui/dark", palette().window().color().lightness() < 128).toBool());
    applyTheme(theme_->isChecked());
    connect(theme_, &QAction::toggled, this, [this](bool on) {
        applyTheme(on);
        QSettings().setValue("ui/dark", on);
        translateChrome();
    });
    connect(lang_, &QAction::triggered, this, [this] { chooseLanguage(!language_.russian()); });
    for (auto a : {grid_, ortho_, polar_, osnap_, lang_, theme_}) {
        auto b = new QToolButton(this);
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        statusBar()->addPermanentWidget(b);
        if (a == osnap_) {
            b->setMenu(snapMenu_);
            b->setPopupMode(QToolButton::MenuButtonPopup);
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QToolButton::customContextMenuRequested, this,
                    [this, b](QPoint p) { snapMenu_->popup(b->mapToGlobal(p)); });
        }
    }
    statusBar()->addPermanentWidget(entityCount_);
    ribbonToggle_ = view_->addAction(QString());
    ribbonToggle_->setObjectName("ribbonToggle");
    ribbonToggle_->setCheckable(true);
    ribbonToggle_->setChecked(cfg.value("ui/ribbon", true).toBool());
    connect(ribbonToggle_, &QAction::toggled, this, [this](bool on) {
        applyRibbonMode(on);
        translateChrome();
    });
    buildRibbon();
    if (ribbonToggle_->isChecked())
        applyRibbonMode(true);
    else
        ribbon_->hide();
    connect(layers_, &LayerManager::message, this, [this] { syncRibbon(); });
    connect(ribbon_, &Ribbon::layerPicked, this, [this](const QString& name) {
        for (auto id : session_->document().layerIds())
            if (session_->document().layer(id)->name == name.toStdString()) {
                const auto reply = session_->setLayer(canvas_->selectedIds(), id);
                page_->append(reply);
                return;
            }
    });
    connect(ribbon_, &Ribbon::colorPicked, this, [this](const QVariant& value) {
        pd::Color color;
        bool literal = false;
        if (value.canConvert<QColor>() && value.value<QColor>().isValid()) {
            auto c = value.value<QColor>();
            color = pd::Color{static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                              static_cast<std::uint8_t>(c.blue())};
            literal = true;
        }
        auto ids = canvas_->selectedIds();
        const auto reply = literal ? session_->setColor(ids, color)
                                   : value.toString() == "ByBlock" ? session_->setColor(ids, pd::ByBlock{})
                                                                   : session_->setColor(ids, pd::ByLayer{});
        page_->append(reply);
    });
    connect(ribbon_, &Ribbon::weightPicked, this, [this](const QVariant& value) {
        auto ids = canvas_->selectedIds();
        bool ok = false;
        const double mm = value.toDouble(&ok);
        const auto reply = ok ? session_->setLineWeight(ids, mm)
                              : value.toString() == "ByBlock"
                                    ? session_->setLineWeight(ids, pd::ByBlock{})
                                    : session_->setLineWeight(ids, pd::ByLayer{});
        page_->append(reply);
    });
}

void MainWindow::buildRibbon() {
    ribbon_->addPage("home");
    ribbon_->addPage("insert");
    ribbon_->addPage("annotate");
    ribbon_->addPage("view");
    ribbon_->addPage("manage");
    auto* separator = new QAction(ribbon_);
    separator->setSeparator(true);
    ribbon_->setQuickAccess({new_, open_, save_, exportPdf_, separator, undo_, redo_});
    auto act = [this](const char* name) -> QAction* { return commands_.value(QString::fromLatin1(name)); };
    auto list = [&](std::initializer_list<const char*> names) {
        QList<QAction*> actions;
        for (auto* name : names)
            if (auto* a = act(name))
                actions << a;
        return actions;
    };
    ribbon_->addPanel("home", QT_TRANSLATE_NOOP("MainWindow", "Draw"),
                      list({"LINE", "PLINE", "CIRCLE", "ARC", "RECTANG", "POLYGON", "HATCH"}), 4);
    ribbon_->addPanel("home", QT_TRANSLATE_NOOP("MainWindow", "Modify"),
                      list({"MOVE", "COPY", "ROTATE", "SCALE", "MIRROR", "TRIM", "EXTEND", "OFFSET",
                            "FILLET", "CHAMFER", "ARRAY", "MATCHPROP", "ERASE", "EXPLODE"}),
                      5);
    ribbon_->addPanel("home", QT_TRANSLATE_NOOP("MainWindow", "Annotate"),
                      list({"DIMLINEAR", "DIMALIGNED", "DIMANGULAR", "DIMRADIUS", "DIMDIAMETER",
                            "DIMSTYLE", "DIST", "AREA"}),
                      4);
    ribbon_->addPanelWidget("home", QT_TRANSLATE_NOOP("MainWindow", "Layers"), ribbon_->layerBox());
    ribbon_->addPanel("home", QT_TRANSLATE_NOOP("MainWindow", "Blocks"), list({"BLOCK", "INSERT"}));
    ribbon_->addPanelWidget("home", QT_TRANSLATE_NOOP("MainWindow", "Properties"), ribbon_->colorBox());
    ribbon_->addPanelWidget("home", QT_TRANSLATE_NOOP("MainWindow", "Line weight"), ribbon_->weightBox());
    ribbon_->addPanel("insert", QT_TRANSLATE_NOOP("MainWindow", "Block"), list({"BLOCK", "INSERT"}));
    ribbon_->addPanel("insert", QT_TRANSLATE_NOOP("MainWindow", "Clipboard"),
                      list({"COPYCLIP", "CUTCLIP", "PASTECLIP"}));
    ribbon_->addPanel("insert", QT_TRANSLATE_NOOP("MainWindow", "Import and scripts"),
                      {open_, save_, runScript_}, 2);
    ribbon_->addPanel("annotate", QT_TRANSLATE_NOOP("MainWindow", "Dimensions"),
                      list({"DIMLINEAR", "DIMALIGNED", "DIMANGULAR", "DIMRADIUS", "DIMDIAMETER",
                            "DIMSTYLE"}),
                      3);
    ribbon_->addPanel("annotate", QT_TRANSLATE_NOOP("MainWindow", "Measure"),
                      list({"DIST", "AREA", "HATCH"}));
    ribbon_->addPanel("view", QT_TRANSLATE_NOOP("MainWindow", "Zoom and grid"), {extents_, grid_});
    ribbon_->addPanel("view", QT_TRANSLATE_NOOP("MainWindow", "Drafting aids"),
                      {ortho_, polar_, osnap_, dynamic_, ribbonToggle_});
    ribbon_->addPanel("view", QT_TRANSLATE_NOOP("MainWindow", "Appearance"), {theme_, panels_->menuAction()});
    ribbon_->addPanel("manage", QT_TRANSLATE_NOOP("MainWindow", "Plugins"), {load_, unload_, loadFolder_});
    ribbon_->addPanel("manage", QT_TRANSLATE_NOOP("MainWindow", "Scripts"), {runScript_});
    ribbon_->addPanel("manage", QT_TRANSLATE_NOOP("MainWindow", "Application"), {quit_});
    ribbon_->setPageTitles({QT_TRANSLATE_NOOP("MainWindow", "Home"),
                            QT_TRANSLATE_NOOP("MainWindow", "Insert"),
                            QT_TRANSLATE_NOOP("MainWindow", "Annotate"),
                            QT_TRANSLATE_NOOP("MainWindow", "View"),
                            QT_TRANSLATE_NOOP("MainWindow", "Manage")});
    syncRibbon();
}
void MainWindow::showCoordinates(bool valid, double x, double y) {
    coords_->setText(valid ? QString("X %1   Y %2").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3)
                           : QStringLiteral("X —   Y —"));
}
void MainWindow::updateSnap() {
    unsigned mask = 0;
    for (auto a : snapMenu_->actions())
        if (a->isChecked())
            mask |= a->data().toUInt();
    if (canvas_)
        canvas_->setSnapping(osnap_->isChecked(), static_cast<pd::SnapType>(mask));
    QSettings().setValue("view/snapTypes", mask);
}
void MainWindow::translateChrome() {
    QStringList groups{tr("File"), tr("Drawing"), tr("Editing"), tr("Annotations"), tr("View")};
    for (int i = 0; i < bars_.size(); ++i)
        bars_[i]->setWindowTitle(groups[i]);
    panels_->setTitle(tr("Panels and toolbars"));
    snapMenu_->setTitle(tr("Object snap types"));
    QStringList names{tr("Endpoint"), tr("Midpoint"), tr("Centre"),         tr("Intersection"),
                      tr("Nearest"),  tr("Quadrant"), tr("Insertion point")};
    int i = 0;
    for (auto a : snapMenu_->actions())
        a->setText(names[i++]);
    historyToggle_->setText(tr("Command history"));
    historyToggle_->setToolTip(tr("Expand the command history (F2, Ctrl+9)"));
    grid_->setText(tr("GRID"));
    grid_->setToolTip(tr("Show grid (F7)"));
    osnap_->setText(tr("OSNAP"));
    osnap_->setToolTip(tr("Object snaps (F3). Right-click to choose types."));
    ortho_->setIconText(tr("ORTHO"));
    ortho_->setToolTip(tr("Orthogonal tracking (F8)"));
    polar_->setIconText(tr("POLAR"));
    polar_->setToolTip(tr("Polar tracking (F10)"));
    lang_->setText(language_.russian() ? "RU" : "EN");
    lang_->setToolTip(tr("Switch interface language"));
    theme_->setText(theme_->isChecked() ? tr("Dark") : tr("Light"));
    theme_->setToolTip(tr("Switch light / dark theme"));
    coords_->setToolTip(tr("Cursor in drawing coordinates"));
}
void MainWindow::applyTheme(bool dark) {
    auto p = qApp->style()->standardPalette();
    p.setColor(QPalette::Window, dark ? QColor("#252d35") : QColor("#edf1f4"));
    p.setColor(QPalette::Base, dark ? QColor("#1b2229") : QColor("#ffffff"));
    p.setColor(QPalette::AlternateBase, dark ? QColor("#252d35") : QColor("#f2f6f8"));
    p.setColor(QPalette::Button, p.window().color());
    p.setColor(QPalette::Highlight, dark ? QColor("#266f73") : QColor("#166875"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    for (auto r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(r, dark ? QColor("#e4ebed") : QColor("#243844"));
        p.setColor(QPalette::Disabled, r, dark ? QColor("#86969f") : QColor("#75838c"));
    }
    p.setColor(QPalette::ToolTipBase, p.base().color());
    p.setColor(QPalette::ToolTipText, p.text().color());
    p.setColor(QPalette::PlaceholderText, dark ? QColor("#acb9bd") : QColor("#526269"));
    qApp->setPalette(p); // triggers PaletteChange; DraftIcon reads the palette at paint time.
    update();
}
