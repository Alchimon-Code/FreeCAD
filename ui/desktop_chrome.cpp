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
#include <QPainterPath>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QSvgRenderer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
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
namespace {
// AutoCAD's status bar is a row of flat cells: a hairline between them and, for a pressed toggle, a
// coloured underline instead of a raised button. The native drawing is kept and only the two lines
// are added, so the OSNAP menu arrow and the theme's own text rendering stay intact.
// A status bar cell in AutoCAD carries a small glyph beside its word, and the glyph belongs to the
// theme rather than to a file: it is painted from the palette at paint time, the way the command
// icons are, so switching to the dark theme does not need it rebuilt.
class GlyphIcon : public QIconEngine {
  public:
    enum Kind { Ortho, Polar, Snap, Dynamic, Ribbon, Theme, Plug, Unplug, Folder, Script, Exit };
    explicit GlyphIcon(Kind kind) : kind_(kind) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        const QPalette palette = qApp->palette();
        QColor ink = mode == QIcon::Disabled ? palette.color(QPalette::Disabled, QPalette::WindowText)
                                             : palette.color(QPalette::WindowText);
        QPen pen(ink, 1.3);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / 12.0, rect.height() / 12.0);
        switch (kind_) {
        case Ortho: {
            // A right angle inside a right angle: Ortho's own mark.
            QPolygonF outer;
            outer << QPointF(2, 11) << QPointF(2, 2) << QPointF(11, 2);
            QPolygonF inner;
            inner << QPointF(2, 7) << QPointF(7, 7) << QPointF(7, 2);
            painter->drawPolyline(outer);
            painter->drawPolyline(inner);
            break;
        }
        case Polar:
            // Two rays and the arc between them.
            painter->drawLine(QPointF(2, 10), QPointF(11, 10));
            painter->drawLine(QPointF(2, 10), QPointF(8.4, 3.6));
            painter->drawArc(QRectF(-4, 4, 12, 12), 0, 45 * 16);
            break;
        case Dynamic:
            // The crosshair and the box that carries its length and angle.
            painter->drawLine(QPointF(1, 6), QPointF(5, 6));
            painter->drawLine(QPointF(3, 4), QPointF(3, 8));
            painter->drawRect(QRectF(5, 4, 6.5, 4));
            break;
        case Ribbon:
            // A window with the ribbon's band across its top.
            painter->drawRect(QRectF(1.5, 2.5, 9, 7));
            painter->drawLine(QPointF(1.5, 5.5), QPointF(10.5, 5.5));
            break;
        case Theme:
            // A disc, half filled: the light and the dark of the same picture.
            painter->drawEllipse(QRectF(2, 2, 8, 8));
            painter->setBrush(QBrush(ink));
            painter->drawPie(QRectF(2, 2, 8, 8), 90 * 16, 180 * 16);
            break;
        case Plug: {
            // A plug seen from the side: body, two prongs and the cable leaving it.
            painter->drawRect(QRectF(3, 5, 6, 4));
            painter->drawLine(QPointF(4.5, 1.5), QPointF(4.5, 5));
            painter->drawLine(QPointF(7.5, 1.5), QPointF(7.5, 5));
            QPainterPath cable;
            cable.moveTo(6, 9);
            cable.cubicTo(6, 11, 9.5, 11, 9.5, 8.5);
            painter->drawPath(cable);
            break;
        }
        case Unplug: {
            // The same plug, pulled out: the prongs are clear of the socket.
            painter->drawRect(QRectF(3, 6, 6, 4));
            painter->drawLine(QPointF(4.5, 2.5), QPointF(4.5, 4.5));
            painter->drawLine(QPointF(7.5, 2.5), QPointF(7.5, 4.5));
            QPainterPath cable;
            cable.moveTo(6, 10);
            cable.cubicTo(6, 11.5, 9.5, 11.5, 9.5, 9.5);
            painter->drawPath(cable);
            break;
        }
        case Folder:
            painter->drawPolygon(QPolygonF({{2, 10}, {2, 3}, {5, 3}, {6.5, 5}, {10, 5}, {10, 10}}),
                                 Qt::WindingFill);
            break;
        case Script:
            // A page and the play mark on it: run this file.
            painter->drawPolygon(QPolygonF({{2, 2}, {7, 2}, {9, 4}, {9, 10}, {2, 10}}), Qt::WindingFill);
            painter->setBrush(QBrush(ink));
            painter->drawPolygon(QPolygonF({{4.5, 4.5}, {7.5, 6}, {4.5, 7.5}}), Qt::WindingFill);
            break;
        case Exit: {
            // A door and an arrow leaving through it.
            painter->drawPolyline(QPolygonF({{6, 2}, {2, 2}, {2, 10}, {6, 10}}));
            painter->drawLine(QPointF(7, 6), QPointF(11, 6));
            painter->drawPolyline(QPolygonF({{9, 4}, {11, 6}, {9, 8}}));
            break;
        }
        case Snap:
            // The object snap aperture: a box with four ticks.
            painter->drawRect(QRectF(3, 3, 6, 6));
            painter->drawLine(QPointF(6, 0.5), QPointF(6, 3));
            painter->drawLine(QPointF(6, 9), QPointF(6, 11.5));
            painter->drawLine(QPointF(0.5, 6), QPointF(3, 6));
            painter->drawLine(QPointF(9, 6), QPointF(11.5, 6));
            break;
        }
        painter->restore();
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pix(size);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pix;
    }
    QIconEngine* clone() const override { return new GlyphIcon(kind_); }

  private:
    Kind kind_;
};

class StatusCell : public QToolButton {
  public:
    using QToolButton::QToolButton;

  protected:
    void paintEvent(QPaintEvent* event) override {
        QToolButton::paintEvent(event);
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawLine(width() - 1, 3, width() - 1, height() - 4);
        if (isChecked()) {
            painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
            painter.drawLine(2, height() - 1, width() - 3, height() - 1);
        }
    }
};
} // namespace

void MainWindow::setupChrome() {
    Q_INIT_RESOURCE(pd_icons);
    QSettings cfg;
    panels_ = view_->addMenu(QString());
    for (auto dock : {static_cast<QDockWidget*>(inspector_), static_cast<QDockWidget*>(layers_), console_})
        panels_->addAction(dock->toggleViewAction());
    // Ctrl+1 properties, Ctrl+2 layers and Ctrl+9 the command line, the three AutoCAD palette keys
    // whose palettes exist here.
    inspector_->toggleViewAction()->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    layers_->toggleViewAction()->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    console_->toggleViewAction()->setShortcut(QKeySequence(QStringLiteral("Ctrl+9")));
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
    // F2 is the text window in AutoCAD; here it collapses the prompt to a single line and expands it
    // back into a scrolling history.
    historyToggle_ = view_->addAction(QString());
    historyToggle_->setObjectName("commandHistoryToggle");
    historyToggle_->setCheckable(true);
    historyToggle_->setChecked(cfg.value("view/commandHistory", true).toBool());
    historyToggle_->setShortcut(QKeySequence(Qt::Key_F2));
    connect(historyToggle_, &QAction::toggled, this, &MainWindow::showCommandHistory);
    history_->setVisible(historyToggle_->isChecked());
    // Ctrl+0 in AutoCAD: the drawing and nothing else. The command line stays, because typing is how
    // the drawing is driven.
    cleanScreen_ = view_->addAction(QString());
    cleanScreen_->setObjectName("cleanScreen");
    cleanScreen_->setCheckable(true);
    cleanScreen_->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(cleanScreen_, &QAction::toggled, this, &MainWindow::applyCleanScreen);
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
    // AutoCAD keeps the coordinate readout in the lower left corner of the window and its messages
    // beside it, so the readout never disappears under a hint the way a temporary message would.
    coords_ = new QLabel(this);
    coords_->setObjectName("worldCoordinates");
    coords_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    statusBar()->addWidget(coords_);
    statusMessage_ = new QLabel(this);
    statusMessage_->setObjectName("statusMessage");
    statusMessage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(statusMessage_, 1);
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
    // The three drawing aids get a glyph of their own; the grid already carries one from the command
    // icons, and the language and theme cells stay words because they name no drawing aid.
    ortho_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Ortho)));
    polar_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Polar)));
    osnap_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Snap)));
    // The other ribbon toggles on the View page carry a mark too, so no panel mixes glyphs and bare
    // words.
    dynamic_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Dynamic)));
    theme_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Theme)));
    // The Manage page is all words otherwise: the plugin pair, the folder, the script runner and the
    // way out each get a mark of their own.
    load_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Plug)));
    unload_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Unplug)));
    loadFolder_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Folder)));
    runScript_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Script)));
    quit_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Exit)));
    for (auto a : {grid_, ortho_, polar_, osnap_, lang_, theme_}) {
        auto b = new StatusCell(this);
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        b->setIconSize({12, 12});
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
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
    ribbonToggle_->setIcon(QIcon(new GlyphIcon(GlyphIcon::Ribbon)));
    ribbonToggle_->setChecked(cfg.value("ui/ribbon", true).toBool());
    connect(ribbonToggle_, &QAction::toggled, this, [this](bool on) {
        applyRibbonMode(on);
        translateChrome();
    });
    buildRibbon();
    if (ribbonToggle_->isChecked())
        applyRibbonMode(true);
    else
        setRibbonVisible(false);
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
                      list({"LINE", "PLINE", "CIRCLE", "ARC", "RECTANG", "POLYGON", "HATCH"}), 3);
    ribbon_->addPanel("home", QT_TRANSLATE_NOOP("MainWindow", "Modify"),
                      list({"MOVE", "COPY", "ROTATE", "SCALE", "MIRROR", "TRIM", "EXTEND", "OFFSET",
                            "FILLET", "CHAMFER", "ARRAY", "MATCHPROP", "ERASE", "EXPLODE"}),
                      4);
    ribbon_->addPanel("home", QT_TRANSLATE_NOOP("MainWindow", "Annotate"),
                      list({"DIMLINEAR", "DIMALIGNED", "DIMANGULAR", "DIMRADIUS", "DIMDIAMETER",
                            "DIMSTYLE", "DIST", "AREA"}),
                      3);
    ribbon_->addPanelWidget("home", QT_TRANSLATE_NOOP("MainWindow", "Layers"), ribbon_->layerBox());
    ribbon_->addPanel("home", QT_TRANSLATE_NOOP("MainWindow", "Blocks"), list({"BLOCK", "INSERT"}));
    // Colour and line weight live in one Properties group, the way AutoCAD stacks them, which also
    // buys the room for the Blocks panel on a 1520 px window.
    auto* properties = new QWidget;
    auto* propertiesBox = new QVBoxLayout(properties);
    propertiesBox->setContentsMargins(0, 0, 0, 0);
    propertiesBox->setSpacing(4);
    propertiesBox->addWidget(ribbon_->colorBox());
    propertiesBox->addWidget(ribbon_->weightBox());
    ribbon_->addPanelWidget("home", QT_TRANSLATE_NOOP("MainWindow", "Properties"), properties);
    // The Insert page carries what AutoCAD's does: the block pair, the clipboard with its base point,
    // the drawing itself and the ways out of it. Four panels instead of three keeps the page honest
    // about where a command lives.
    ribbon_->addPanel("insert", QT_TRANSLATE_NOOP("MainWindow", "Block"), list({"BLOCK", "INSERT"}));
    // The clipboard entries are the Edit menu's own actions: the command names COPYCLIP and friends are
    // not in the vocabulary the panels look up, so asking for them by name made an empty panel.
    ribbon_->addPanel("insert", QT_TRANSLATE_NOOP("MainWindow", "Clipboard"),
                      {copy_, cut_, paste_, copyBase_}, 2);
    ribbon_->addPanel("insert", QT_TRANSLATE_NOOP("MainWindow", "Drawings"),
                      {new_, open_, save_, saveAs_}, 2, 2);
    ribbon_->addPanel("insert", QT_TRANSLATE_NOOP("MainWindow", "Export and scripts"),
                      {exportPdf_, runScript_}, 1);
    ribbon_->addPanel("annotate", QT_TRANSLATE_NOOP("MainWindow", "Dimensions"),
                      list({"DIMLINEAR", "DIMALIGNED", "DIMANGULAR", "DIMRADIUS", "DIMDIAMETER",
                            "DIMSTYLE"}),
                      3);
    ribbon_->addPanel("annotate", QT_TRANSLATE_NOOP("MainWindow", "Measure"),
                      list({"DIST", "AREA", "HATCH"}));
    ribbon_->addPanel("view", QT_TRANSLATE_NOOP("MainWindow", "Zoom and grid"), {extents_, grid_});
    // The drafting aids stand two to a row, as AutoCAD's do: one column of six made the View page the
    // tallest panel on the ribbon and pushed the captions far from their buttons.
    ribbon_->addPanel("view", QT_TRANSLATE_NOOP("MainWindow", "Drafting aids"),
                      {ortho_, polar_, osnap_, dynamic_, ribbonToggle_}, 2);
    ribbon_->addPanel("view", QT_TRANSLATE_NOOP("MainWindow", "Appearance"),
                      {theme_, panels_->menuAction(), cleanScreen_}, 2);
    // The plugin commands stand two to a row: three words in one column made the Manage page the widest
    // panel on the ribbon for the least content.
    ribbon_->addPanel("manage", QT_TRANSLATE_NOOP("MainWindow", "Plugins"), {load_, unload_, loadFolder_},
                      2);
    ribbon_->addPanel("manage", QT_TRANSLATE_NOOP("MainWindow", "Scripts"), {runScript_});
    ribbon_->addPanel("manage", QT_TRANSLATE_NOOP("MainWindow", "Palettes"),
                      {inspector_->toggleViewAction(), layers_->toggleViewAction(),
                       console_->toggleViewAction()},
                      1, 0);
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
    historyToggle_->setToolTip(tr("Expand the command history (F2)"));
    cleanScreen_->setText(tr("Clean screen"));
    cleanScreen_->setToolTip(tr("Hide the ribbon, palettes and menus (Ctrl+0)"));
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
