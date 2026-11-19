// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include "language.h"
#include "layer_manager.h"
#include "main_window.h"
#include "property_inspector.h"
#include "viewport.h"
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QIconEngine>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QSvgRenderer>
#include <QToolBar>
#include <QToolButton>
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
        data.replace("#233747", ink.name().toLatin1());
        data.replace("#007f81", mode == QIcon::Disabled ? ink.name().toLatin1()
                                : dark                  ? "#77dbce"
                                                        : "#00747a");
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
    for (auto a : editActions_) {
        auto k = a->data().toString().toLower();
        if (QStringList{"line", "block", "insert"}.contains(k))
            add(1, a, k);
        else if (QStringList{"move", "copy", "rotate", "scale", "mirror", "trim", "extend", "offset",
                             "fillet", "erase", "explode"}
                     .contains(k))
            add(2, a, k);
        else if (k.startsWith("dim") || k == "hatch")
            add(3, a, k);
        else if (k == "layout")
            add(4, a, k);
    }
    add(4, extents_, "extents");
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
