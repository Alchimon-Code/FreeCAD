// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewport.h"
#include "qt_text.hpp"
#include "scene_renderer.hpp"

#include <QCoreApplication>
#include <QLabel>
#include <QApplication>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
pd::Vec2 pixel(QPointF p) { return {p.x(), p.y()}; }

// The snap names are the ones the object-snap menu carries, so the word beside the marker is the same
// word the user picked in that menu, in whatever language the menu is in.
QString snapName(pd::SnapType type) {
    auto name = [](const char* source) { return QCoreApplication::translate("MainWindow", source); };
    switch (type) {
    case pd::SnapType::Endpoint:
        return name("Endpoint");
    case pd::SnapType::Midpoint:
        return name("Midpoint");
    case pd::SnapType::Center:
        return name("Centre");
    case pd::SnapType::Quadrant:
        return name("Quadrant");
    case pd::SnapType::Insertion:
        return name("Insertion");
    case pd::SnapType::Intersection:
        return name("Intersection");
    case pd::SnapType::Nearest:
        return name("Nearest");
    default:
        return {};
    }
}

// AutoCAD's UCS icon: two arrows from the world origin, X to the right and Y upwards, drawn over the
// drawing area. It is a child widget, the way the dynamic-input label is, so it never disturbs the
// OpenGL pass that paints the drawing itself.
class UcsIcon final : public QWidget {
  public:
    explicit UcsIcon(QWidget* parent) : QWidget(parent) {
        setObjectName("ucsIcon");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFixedSize(54, 54);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        // The drawing area is dark in both themes, so the icon keeps a light grey of its own.
        painter.setPen(QPen(QColor(206, 216, 221), 1.2));
        const QPoint base(16, height() - 16);
        const QPoint xTip(base.x() + 30, base.y());
        const QPoint yTip(base.x(), base.y() - 30);
        painter.drawLine(base, xTip);
        painter.drawLine(base, yTip);
        painter.drawLine(xTip, xTip + QPoint(-6, -3));
        painter.drawLine(xTip, xTip + QPoint(-6, 3));
        painter.drawLine(yTip, yTip + QPoint(3, 6));
        painter.drawLine(yTip, yTip + QPoint(-3, 6));
        QFont font = painter.font();
        font.setPixelSize(11);
        painter.setFont(font);
        painter.drawText(xTip + QPoint(3, 4), QStringLiteral("X"));
        painter.drawText(yTip + QPoint(-11, -4), QStringLiteral("Y"));
    }
};
} // namespace
Viewport::Viewport(const pd::Document& doc, QWidget* parent)
    : QOpenGLWidget(parent), doc_(doc), snaps_(pd::qtTextStrokes), selection_(pd::qtTextStrokes) {
    setFormat(requestedFormat());
    setMinimumSize(240, 160);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    camera_.resize(width(), height());
    ensureDynamicInput();
    zoomTimer_.setTimerType(Qt::PreciseTimer);
    zoomTimer_.setInterval(12);
    connect(&zoomTimer_, &QTimer::timeout, this, [this] {
        const double t = std::min(1.0, zoomTime_.elapsed() / 110.0);
        const double ease = 1 - std::pow(1 - t, 3);
        camera_ = zoomBase_; // compute from the initial anchor, not from rounded animation frames
        camera_.zoomAt(zoomAnchor_, std::pow(targetScale_ / zoomBase_.scale(), ease));
        if (t == 1) {
            zoomTimer_.stop();
        }
        refreshCursor();
    });
}
Viewport::~Viewport() { releaseGL(); }
QSurfaceFormat Viewport::requestedFormat() {
    QSurfaceFormat f;
    f.setRenderableType(QSurfaceFormat::OpenGL);
    f.setVersion(4, 5);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setSwapInterval(1);
    return f;
}
void Viewport::initializeGL() {
    const auto f = context()->format();
    if (context()->isOpenGLES() || f.version() < qMakePair(4, 5) ||
        f.profile() != QSurfaceFormat::CoreProfile) {
        emit rendererError(tr("OpenGL 4.5 Core is unavailable."));
        return;
    }
    auto renderer = std::make_unique<pd::SceneRenderer>();
    QString log;
    if (!renderer->initialize(log)) {
        emit rendererError(log.isEmpty() ? tr("OpenGL 4.5 Core is unavailable.")
                                         : tr("Shader error: %1").arg(log));
        return;
    }
    renderer_ = std::move(renderer);
    cleanup_ = connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &Viewport::releaseGL,
                       Qt::DirectConnection);
}
void Viewport::releaseGL() {
    if (renderer_) {
        disconnect(cleanup_);
        makeCurrent();
        renderer_.reset();
        doneCurrent();
    }
}
void Viewport::paintGL() {
    if (renderer_) {
        const auto dpr = devicePixelRatioF();
        // Match QOpenGLWidget's rounded FBO size: an implicit int(width * dpr) leaves
        // the last column or row unpainted at fractional scale and odd logical sizes.
        const QSize frame = size() * dpr;
        context()->functions()->glViewport(0, 0, frame.width(), frame.height());
        if (page_) {
            renderer_->drawPage(doc_, *page_, camera_, dpr);
            return;
        }
        renderer_->draw(doc_, camera_, dpr, selection_, overlay_,
                        palette().window().color().lightness() < 128, editor_ ? editor_->ghost() : nullptr,
                        grid_);
    }
}
void Viewport::showEvent(QShowEvent* event) {
    QOpenGLWidget::showEvent(event);
    // A restored dock layout may still post a resize after showEvent (e.g. a DXF from Explorer).
    if (fitOnShow_)
        QTimer::singleShot(0, this, [this] {
            if (fitOnShow_ && isVisible())
                zoomExtents();
        });
}
void Viewport::resizeEvent(QResizeEvent* e) {
    zoomTimer_.stop();
    camera_.resize(width(), height());
    ensureDynamicInput();
    refreshCursor();
    QOpenGLWidget::resizeEvent(e);
}
std::vector<pd::EntityId> Viewport::selectedIds() const {
    if (!selection_.usesModelFlags())
        return {selection_.ids().begin(), selection_.ids().end()};
    if (!modelSelection_) {
        modelSelection_.emplace();
        for (auto id : doc_.entityIds())
            if (doc_.entity(id)->traits.isSelected)
                modelSelection_->insert(id);
    }
    std::vector<pd::EntityId> ids;
    for (auto id : *modelSelection_)
        if (auto e = doc_.entity(id); e && doc_.visible(*e))
            ids.push_back(id);
    return ids;
}
void Viewport::zoomExtents() {
    fitOnShow_ = !isVisible();
    zoomTimer_.stop();
    camera_.resize(width(), height());
    ensureDynamicInput();
    camera_.fit(page_ ? pd::BoundingBox2D({}, page_->size()) : doc_.extents());
    refreshCursor();
}
void Viewport::invalidate(std::span<const pd::Change> changes) {
    if (!changes.empty() && changes.front().kind == pd::Change::Kind::Reset) {
        selection_ = pd::SelectionManager(pd::qtTextStrokes);
        modelSelection_.reset();
        overlay_ = {};
        press_.reset();
        pan_.reset();
        gripPressed_ = false;
        setCursor(Qt::BlankCursor);
        camera_ = pd::Camera2D{};
        zoomExtents();
    } else {
        if (modelSelection_)
            for (const auto& c : changes)
                if (c.object == pd::Change::Object::Entity) {
                    auto e = doc_.entity(c.id);
                    if (e && e->traits.isSelected)
                        modelSelection_->insert(c.id);
                    else
                        modelSelection_->erase(c.id);
                }
        selection_.reconcile(doc_, changes);
    }
    if (renderer_) {
        renderer_->invalidate(doc_, changes, selection_);
    }
    refreshCursor(); // even if the mouse has not moved since Undo
}
void Viewport::setSelection(pd::SelectionManager::Ids ids) {
    std::erase_if(ids, [&](auto id) {
        auto e = doc_.entity(id);
        return !e || !doc_.visible(*e) || page_;
    });
    replaceSelection(std::move(ids));
}
void Viewport::replaceSelection(pd::SelectionManager::Ids next) {
    modelSelection_.reset();
    auto dirty = selection_.replace(std::move(next));
    if (renderer_) {
        renderer_->invalidateSelection(dirty);
    }
    emit selectionChanged();
    update();
}
void Viewport::refreshCursor() {
    auto point = overlay_.cursor ? camera_.screenToWorld(*overlay_.cursor) : pd::Vec2{};
    emit cursorPosition(overlay_.cursor.has_value(), point.x, point.y);
    if (!ucsIcon_)
        ucsIcon_ = new UcsIcon(this);
    refreshUcsIcon();
    if (page_) {
        if (snapHint_)
            snapHint_->hide();
        update();
        return;
    }
    bool pickEntity = editor_ && editor_->pickingEntity();
    // A side pick just inside the aperture must not snap onto the curve and lose its side.
    const bool pickingSide = editor_ && editor_->step() == EditController::Step::Side;
    overlay_.snap =
        overlay_.cursor && !pan_ && !overlay_.frameStart && !pickingSide && (pickEntity || snapEnabled_)
            ? snaps_.find(doc_, camera_, *overlay_.cursor, overlay_.aperture,
                          pickEntity ? pd::SnapType::Nearest : snapMask_, editor_ ? editor_->hotId() : 0)
            : std::nullopt;
    if (editor_) {
        std::optional<pd::Vec2> world;
        if (overlay_.cursor && !pan_)
            world = overlay_.snap ? overlay_.snap->point : camera_.screenToWorld(*overlay_.cursor);
        editor_->hover(world, overlay_.snap ? std::optional{overlay_.snap->id} : std::nullopt);
        overlay_.guideBase = editor_->guideBase();
        overlay_.effective = editor_->cursorPoint();
        overlay_.hot = editor_->hotPoint();
        if (overlay_.snap && overlay_.effective &&
            (*overlay_.effective - overlay_.snap->point).length() > pd::EPSILON)
            overlay_.snap.reset();
    }
    refreshDynamicInput();
    refreshSnapHint();
    update();
}
void Viewport::wheelEvent(QWheelEvent* e) {
    e->accept();
    if (press_ || pan_) {
        return;
    }
    zoomAnchor_ = pixel(e->position());
    overlay_.cursor = zoomAnchor_;
    const auto delta = e->pixelDelta().isNull() ? e->angleDelta().y() / 120.0 : e->pixelDelta().y() / 60.0;
    if (delta == 0) {
        return;
    }
    fitOnShow_ = false;
    const double base = zoomTimer_.isActive() ? targetScale_ : camera_.scale();
    targetScale_ = std::clamp(base * std::pow(1.25, delta), pd::Camera2D::minScale, pd::Camera2D::maxScale);
    zoomBase_ = camera_;
    zoomTime_.start();
    zoomTimer_.start();
}
bool Viewport::selecting() const {
    return !editor_ || !editor_->active() || editor_->step() == EditController::Step::Select;
}
void Viewport::showSpace(std::optional<pd::Layout> page) {
    if (!page_ && !page)
        return;
    zoomTimer_.stop();
    if (editor_ && editor_->active())
        editor_->cancel();
    if (!page_)
        savedModel_ = camera_;
    press_.reset();
    pan_.reset();
    gripPressed_ = false;
    overlay_ = {};
    replaceSelection({});
    page_ = std::move(page);
    if (renderer_)
        renderer_->invalidatePage();
    setCursor(page_ ? Qt::ArrowCursor : Qt::BlankCursor);
    if (page_)
        zoomExtents();
    else {
        camera_ = savedModel_;
        camera_.resize(width(), height());
        ensureDynamicInput();
        refreshCursor();
    }
}
void Viewport::setEditor(EditController* e) {
    if (editor_)
        disconnect(editor_, nullptr, this, nullptr);
    editor_ = e;
    if (e)
        connect(e, &EditController::stateChanged, this, [this] {
            overlay_.frameStart.reset();
            refreshCursor();
        });
    refreshCursor();
}
std::optional<std::pair<pd::EntityId, size_t>> Viewport::gripAt(pd::Vec2 screen) const {
    constexpr double radius = 5;
    auto corner = pd::Vec2{radius, radius};
    auto box = pd::BoundingBox2D::fromPoints(camera_.screenToWorld(screen - corner),
                                             camera_.screenToWorld(screen + corner));
    auto ids = doc_.query(box), extra = doc_.queryExtraPoints(box);
    ids.insert(ids.end(), extra.begin(), extra.end());
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::optional<std::pair<pd::EntityId, size_t>> result;
    double best = std::numeric_limits<double>::infinity();
    for (auto id : ids) {
        auto e = doc_.entity(id);
        if (!selection_.selected(*e) || doc_.layer(e->traits.layer)->isLocked)
            continue;
        auto pts = e->getGripPoints();
        for (size_t i = 0; i < pts.size(); ++i) {
            auto v = camera_.worldToScreen(pts[i]) - screen;
            auto dist = v.length();
            if (std::max(std::abs(v.x), std::abs(v.y)) <= radius && dist < best) {
                best = dist;
                result = {{id, i}};
            }
        }
    }
    return result;
}
void Viewport::mousePressEvent(QMouseEvent* e) {
    if (page_ && e->button() != Qt::MiddleButton) {
        e->accept();
        return;
    }
    zoomTimer_.stop();
    overlay_.cursor = pixel(e->position());
    if (e->button() == Qt::MiddleButton) {
        fitOnShow_ = false;
        pan_ = overlay_.cursor;
        press_.reset();
        overlay_.frameStart.reset();
        setCursor(Qt::ClosedHandCursor);
    } else if (e->button() == Qt::LeftButton && !pan_) {
        press_ = overlay_.cursor;
        gripPressed_ = false;
        if (editor_ && !editor_->active() && e->modifiers() == Qt::NoModifier) {
            if (auto grip = gripAt(*press_)) {
                emit interactionMessage(editor_->beginGrip(grip->first, grip->second));
                gripPressed_ = true;
            }
        }
    } else if (e->button() == Qt::RightButton) {
        emit contextMenuRequested(e->globalPosition().toPoint());
    } else {
        QOpenGLWidget::mousePressEvent(e);
        return;
    }
    refreshCursor();
    e->accept();
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* e) {
    // Classic AutoCAD middle-double-click: zoom to the drawing extents.
    if (e->button() == Qt::MiddleButton) {
        pan_.reset();
        press_.reset();
        zoomExtents();
        e->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(e);
}
void Viewport::mouseMoveEvent(QMouseEvent* e) {
    const auto p = pixel(e->position());
    overlay_.cursor = p;
    if (pan_ && !(e->buttons() & Qt::MiddleButton)) {
        pan_.reset();
        setCursor(page_ ? Qt::ArrowCursor
                        : Qt::BlankCursor); // focus or mouse grab was lost outside the widget
    }
    if (press_ && !(e->buttons() & Qt::LeftButton)) {
        press_.reset();
        overlay_.frameStart.reset();
    }
    if (pan_) {
        camera_.pan(p - *pan_);
        pan_ = p;
    } else if (press_ && selecting() && !gripPressed_ &&
               (p - *press_).length() >= QApplication::startDragDistance()) {
        overlay_.frameStart = press_;
    }
    refreshDynamicInput();
    refreshCursor();
}
void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    const auto p = pixel(e->position());
    overlay_.cursor = p;
    if (e->button() == Qt::MiddleButton && pan_) {
        camera_.pan(p - *pan_);
        pan_.reset();
        setCursor(page_ ? Qt::ArrowCursor : Qt::BlankCursor);
    } else if (e->button() == Qt::LeftButton && press_) {
        const auto start = *press_;
        press_.reset();
        const bool frame = overlay_.frameStart || (p - start).length() >= QApplication::startDragDistance();
        overlay_.frameStart.reset();
        if (gripPressed_ && (!editor_ || !editor_->active() || (p - start).length() < 2)) {
            // A click makes the grip hot; a drag commits on release. Do not commit the activation click.
        } else if (!selecting()) {
            refreshCursor();
            if (auto point = editor_->cursorPoint())
                emit interactionMessage(
                    editor_->pick(*point, overlay_.snap ? std::optional{overlay_.snap->id} : std::nullopt));
        } else {
            pd::SelectionManager::Ids hits;
            if (frame)
                hits = selection_.frame(doc_, camera_, start, p);
            else if (auto hit = snaps_.pick(doc_, camera_, p, overlay_.aperture))
                hits.insert(hit->id);
            if ((editor_ && editor_->active()) ||
                (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
                auto ids = selectedIds();
                pd::SelectionManager::Ids next(ids.begin(), ids.end());
                for (auto id : hits) {
                    if (e->modifiers() & Qt::ShiftModifier)
                        next.erase(id);
                    else
                        next.insert(id);
                }
                replaceSelection(std::move(next));
            } else
                replaceSelection(std::move(hits));
        }
        gripPressed_ = false;
    } else {
        QOpenGLWidget::mouseReleaseEvent(e);
        return;
    }
    if (!rect().contains(e->position().toPoint())) {
        overlay_.cursor.reset();
    }
    refreshCursor();
    e->accept();
}
void Viewport::escape() {
    press_.reset();
    pan_.reset();
    gripPressed_ = false;
    setCursor(page_ ? Qt::ArrowCursor : Qt::BlankCursor);
    overlay_.frameStart.reset();
    if (editor_ && editor_->active())
        emit interactionMessage(editor_->cancel());
    else
        replaceSelection({});
    refreshCursor();
}
void Viewport::keyPressEvent(QKeyEvent* e) {
    if (dynamicInput_ && typeDynamic(e)) {
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier) {
        emit enterRequested();
    } else if (e->key() == Qt::Key_Escape)
        escape();
    else if (editor_ && editor_->active() && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter))
        emit interactionMessage(editor_->enter(selecting() ? selectedIds() : std::vector<pd::EntityId>{}));
    else if (e->key() == Qt::Key_Delete)
        emit eraseRequested();
    else if (!e->text().isEmpty() && e->text().front().isPrint() &&
             !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
        emit commandText(e->text());
    else {
        QOpenGLWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}
void Viewport::enterEvent(QEnterEvent* e) {
    overlay_.cursor = pixel(e->position());
    refreshCursor();
    QOpenGLWidget::enterEvent(e);
}
void Viewport::leaveEvent(QEvent* e) {
    if (!press_ && !pan_) {
        overlay_.cursor.reset();
        refreshCursor();
    }
    QOpenGLWidget::leaveEvent(e);
}

void Viewport::setDynamicInput(bool on) {
    if (dynamicInput_ == on)
        return;
    dynamicInput_ = on;
    dynLength_.clear();
    dynAngle_.clear();
    dynField_ = 0;
    refreshDynamicInput();
    emit dynamicInputChanged(on);
}
QString Viewport::dynamicInputText() const {
    if (!dynInput_ || dynInput_->isHidden())
        return {};
    return dynInput_->text();
}
bool Viewport::typeDynamic(QKeyEvent* e) {
    if (!editor_ || !editor_->active() || !editor_->basePoint()) {
        dynInput_->hide();
        return false;
    }
    if (e->key() == Qt::Key_Tab) {
        dynField_ = dynField_ ? 0 : 1;
        refreshDynamicInput();
        return true;
    }
    if (e->key() == Qt::Key_Backspace) {
        auto& field = dynField_ ? dynAngle_ : dynLength_;
        if (!field.isEmpty())
            field.chop(1);
        refreshDynamicInput();
        return true;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        applyDynamicInput();
        return true;
    }
    const auto text = e->text();
    if (text.isEmpty() || !(text.front().isDigit() || text.front() == '.' || text.front() == '-' ||
                            text.front() == ','))
        return false;
    auto& field = dynField_ ? dynAngle_ : dynLength_;
    if (field.size() < 16)
        field += text.front() == ',' ? QChar('.') : text.front();
    refreshDynamicInput();
    return true;
}
void Viewport::applyDynamicInput() {
    if (!editor_ || !editor_->basePoint())
        return;
    const auto base = *editor_->basePoint();
    const auto cursor = overlay_.cursor ? camera_.screenToWorld(*overlay_.cursor) : base;
    const auto delta = cursor - base;
    bool okLength = false, okAngle = false;
    const double typedLength = dynLength_.toDouble(&okLength);
    const double typedAngle = dynAngle_.toDouble(&okAngle);
    const double length = okLength ? typedLength : delta.length();
    const double angle = okAngle ? typedAngle * 3.14159265358979323846 / 180
                                 : std::atan2(delta.y, delta.x);
    dynLength_.clear();
    dynAngle_.clear();
    dynField_ = 0;
    if (!(length > 0)) {
        refreshDynamicInput();
        return;
    }
    const auto point = base + pd::Vec2{length * std::cos(angle), length * std::sin(angle)};
    auto reply = editor_->pick(point, {});
    overlay_.cursor = camera_.worldToScreen(point);
    emit interactionMessage(reply);
    refreshDynamicInput();
    refreshCursor();
}

// The label lives as long as the viewport. Re-creating it on every resize, zoom-extents and page
// switch piled up hidden children behind the visible one and made findChild("dynamicInput") pick an
// arbitrary label instead of the one that carries the current length and angle.
void Viewport::ensureDynamicInput() {
    if (dynInput_)
        return;
    dynInput_ = new QLabel(this);
    dynInput_->setObjectName("dynamicInput");
    dynInput_->setAttribute(Qt::WA_TransparentForMouseEvents);
    dynInput_->setAttribute(Qt::WA_StyledBackground, true);
    dynInput_->setStyleSheet("QLabel { padding: 2px 6px; border: 1px solid palette(mid);"
                             " background: palette(base); color: palette(text); }");
    dynInput_->hide();
}

// The icon tracks the world origin while it is on screen and stays in the corner of the drawing area
// once the view scrolls it away, which is what AutoCAD's icon does.
void Viewport::refreshUcsIcon() {
    if (!ucsIcon_)
        return;
    const auto origin = camera_.worldToScreen(pd::Vec2{});
    constexpr int kBase = 16; // the origin inside the icon, see UcsIcon::paintEvent
    const int margin = 4;
    const int maxX = std::max(margin, width() - ucsIcon_->width() - margin);
    const int maxY = std::max(margin, height() - ucsIcon_->height() - margin);
    const int x = std::clamp(int(std::lround(origin.x)) - kBase, margin, maxX);
    const int y = std::clamp(int(std::lround(origin.y)) - (ucsIcon_->height() - kBase), margin, maxY);
    ucsIcon_->move(x, y);
    ucsIcon_->raise();
    ucsIcon_->show();
}

// AutoCAD names the point it is about to snap to: a small box beside the marker carries the word.
void Viewport::refreshSnapHint() {
    if (!snapHint_) {
        snapHint_ = new QLabel(this);
        snapHint_->setObjectName("snapHint");
        snapHint_->setAttribute(Qt::WA_TransparentForMouseEvents);
        snapHint_->setAttribute(Qt::WA_StyledBackground, true);
        snapHint_->setStyleSheet("QLabel { padding: 1px 5px; border: 1px solid palette(mid);"
                                 " background: palette(base); color: palette(text); }");
        snapHint_->hide();
    }
    const QString name = overlay_.snap && !overlay_.frameStart ? snapName(overlay_.snap->type) : QString();
    if (page_ || name.isEmpty()) {
        snapHint_->hide();
        return;
    }
    snapHint_->setText(name);
    snapHint_->adjustSize();
    const auto at = camera_.worldToScreen(overlay_.snap->point) + pd::Vec2{14, -snapHint_->height() - 8};
    const int x = std::clamp(int(std::lround(at.x)), 2, std::max(2, width() - snapHint_->width() - 2));
    const int y = std::clamp(int(std::lround(at.y)), 2, std::max(2, height() - snapHint_->height() - 2));
    snapHint_->move(x, y);
    snapHint_->show();
    snapHint_->raise();
}

void Viewport::refreshDynamicInput() {
    if (!dynInput_)
        return;
    const bool show = dynamicInput_ && editor_ && editor_->active() && editor_->basePoint() &&
                      overlay_.cursor.has_value();
    if (!show) {
        dynInput_->hide();
        return;
    }
    auto mark = [this](const QString& s, int field) {
        return dynField_ == field ? QStringLiteral("[%1]").arg(s.isEmpty() ? QStringLiteral("…") : s)
                                  : (s.isEmpty() ? QStringLiteral("…") : s);
    };
    dynInput_->setText(tr("L %1  ∠ %2°").arg(mark(dynLength_, 0), mark(dynAngle_, 1)));
    dynInput_->adjustSize();
    const auto pos = overlay_.cursor.value() + pd::Vec2{18, 18};
    dynInput_->move(QPoint(qRound(pos.x), qRound(pos.y)));
    dynInput_->show();
    dynInput_->raise();
}
