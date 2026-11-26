// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QDockWidget>
#include <QMouseEvent>
#include <QPainter>
#include <functional>

// A palette's own title strip. A parked palette is as wide as this strip, so the label is drawn
// rotated, the way AutoCAD's auto-hidden palettes read; clicked, the strip asks the window to open the
// palette again. The label is the dock's own window title, so whatever names the palette — including
// the translation that renames it — names the strip too.
class PaletteTitle : public QWidget {
  public:
    explicit PaletteTitle(QDockWidget* dock, std::function<void()> onClick, QWidget* parent = nullptr)
        : QWidget(parent), dock_(dock), onClick_(std::move(onClick)) {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QString());
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }
    [[nodiscard]] QSize sizeHint() const override { return {22, 120}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {22, 30}; }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QColor strip = palette().color(QPalette::Window);
        painter.fillRect(rect(), strip.darker(112));
        painter.setPen(strip.lighter(135));
        painter.drawLine(0, 0, 0, height() - 1);
        painter.setPen(palette().color(QPalette::WindowText));
        const QString caption = dock_ ? dock_->windowTitle() : QString();
        if (width() < 40) {
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
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onClick_)
            onClick_();
        QWidget::mousePressEvent(event);
    }

  private:
    QDockWidget* dock_ = nullptr;
    std::function<void()> onClick_;
};
