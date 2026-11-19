// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "edit_controller.hpp"
#include "overlay.hpp"
#include "selection_manager.hpp"
#include <QPointer>

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QTimer>
#include <memory>

namespace pd {
class SceneRenderer;
}

class Viewport final : public QOpenGLWidget {
    Q_OBJECT
  public:
    explicit Viewport(const pd::Document&, QWidget* parent = nullptr);
    ~Viewport() override;
    static QSurfaceFormat requestedFormat();
    [[nodiscard]] std::vector<pd::EntityId> selectedIds() const;
    void invalidate(std::span<const pd::Change> changes);
    void zoomExtents();
    void setEditor(EditController*);
    void showSpace(std::optional<pd::Layout>);
    bool paperSpace() const { return page_.has_value(); }
    void escape();
    void setGrid(bool on) {
        grid_ = on;
        update();
    }
    void setSnapping(bool on, pd::SnapType mask) {
        snapEnabled_ = on;
        snapMask_ = mask;
        refreshCursor();
    }
    void setSelection(pd::SelectionManager::Ids ids);

    [[nodiscard]] const pd::Camera2D& camera() const { return camera_; }
    [[nodiscard]] const pd::SelectionManager& selection() const { return selection_; }
    [[nodiscard]] const std::optional<pd::SnapHit>& snapHit() const { return overlay_.snap; }
  signals:
    void rendererError(const QString& message);
    void interactionMessage(const QString&);
    void commandText(const QString&);
    void eraseRequested();
    void selectionChanged();
    void cursorPosition(bool valid, double x, double y);

  protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

  private:
    void releaseGL();
    void refreshCursor();
    void replaceSelection(pd::SelectionManager::Ids next);
    const pd::Document& doc_;
    pd::Camera2D camera_, zoomBase_, savedModel_;
    std::optional<pd::Layout> page_;
    pd::SnapEngine snaps_;
    pd::SelectionManager selection_;
    // Before the first pick, inspector/SDK selection comes from imported model flags.
    // Build once on demand, then update from change packets rather than scan on each edit.
    mutable std::optional<pd::SelectionManager::Ids> modelSelection_;
    pd::OverlayState overlay_;
    std::unique_ptr<pd::SceneRenderer> renderer_;
    QMetaObject::Connection cleanup_;
    std::optional<pd::Vec2> press_, pan_;
    QPointer<EditController> editor_;
    bool grid_ = true, snapEnabled_ = true, fitOnShow_ = false;
    pd::SnapType snapMask_ = pd::SnapEngine::modes;
    bool gripPressed_ = false;
    bool selecting() const;
    std::optional<std::pair<pd::EntityId, size_t>> gripAt(pd::Vec2 screen) const;
    QTimer zoomTimer_;
    QElapsedTimer zoomTime_;
    pd::Vec2 zoomAnchor_;
    double targetScale_ = 3;
};
