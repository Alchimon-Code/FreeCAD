// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QDockWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <functional>

// A palette's own title strip. A parked palette is as wide as this strip, so the label is drawn
// rotated, the way AutoCAD's auto-hidden palettes read; clicked, the strip asks the window to open the
// palette again. The label is the dock's own window title, so whatever names the palette — including
// the translation that renames it — names the strip too.
class PaletteTitle : public QWidget {
  public:
    explicit PaletteTitle(QDockWidget* dock, std::function<void()> onClick,
                          std::function<void(bool)> onHover = {}, QWidget* parent = nullptr)
        : QWidget(parent), dock_(dock), onClick_(std::move(onClick)), onHover_(std::move(onHover)) {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QString());
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    // Parked, the strip is as tall as the palette beside it; open, it is a title bar one line high.
    // The policy is stated in both states, because a title bar that can grow takes half the palette.
    [[nodiscard]] QSize sizeHint() const override {
        return parked() ? QSize(22, 120) : QSize(120, verticalHeader());
    }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return parked() ? QSize(22, 30) : QSize(60, verticalHeader());
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        const bool nowParked = parked();
        if (nowParked != wasParked_) {
            wasParked_ = nowParked;
            setSizePolicy(QSizePolicy::Preferred, nowParked ? QSizePolicy::Expanding : QSizePolicy::Fixed);
            updateGeometry();
        }
        QWidget::resizeEvent(event);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QColor strip = palette().color(QPalette::Window);
        painter.fillRect(rect(), strip.darker(112));
        painter.setPen(strip.lighter(135));
        painter.drawLine(0, 0, 0, height() - 1);
        painter.setPen(palette().color(QPalette::WindowText));
        const QString caption = dock_ ? dock_->windowTitle() : QString();
        if (parked()) {
            // Parked: the name reads down the strip, as AutoCAD's do.
            painter.save();
            painter.translate(width() / 2.0, height() / 2.0);
            painter.rotate(90);
            const QRect box(-height() / 2, -width() / 2, height(), width());
            painter.drawText(box, Qt::AlignCenter, caption);
            painter.restore();
        } else {
            painter.drawText(rect().adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, caption);
        }
    }
    // AutoCAD's parked palettes open under the pointer and close when it leaves; the strip is what
    // says where the pointer is, and the window decides whether to open or to wait.
    void enterEvent(QEnterEvent* event) override {
        if (onHover_)
            onHover_(true);
        QWidget::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override {
        if (onHover_)
            onHover_(false);
        QWidget::leaveEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onClick_)
            onClick_();
        QWidget::mousePressEvent(event);
    }

  private:
    [[nodiscard]] bool parked() const { return width() < 40; }
    [[nodiscard]] int verticalHeader() const { return fontMetrics().height() + 6; }
    QDockWidget* dock_ = nullptr;
    bool wasParked_ = false;
    std::function<void()> onClick_;
    std::function<void(bool)> onHover_;
};
