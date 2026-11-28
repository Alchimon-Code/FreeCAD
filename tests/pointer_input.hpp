// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "viewport.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
namespace input_test {
inline void mouse(Viewport& view, QEvent::Type type, pd::Vec2 world, Qt::MouseButton button,
                  Qt::MouseButtons down, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    auto p = view.camera().worldToScreen(world);
    QPointF s(p.x, p.y);
    QMouseEvent event(type, s, s, button, down, modifiers);
    QApplication::sendEvent(&view, &event);
}
inline void press(Viewport& v, pd::Vec2 p) {
    mouse(v, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
}
inline void release(Viewport& v, pd::Vec2 p) {
    mouse(v, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}
inline void click(Viewport& v, pd::Vec2 p) {
    press(v, p);
    release(v, p);
}
inline void move(Viewport& v, pd::Vec2 p, bool down = false) {
    mouse(v, QEvent::MouseMove, p, Qt::NoButton, down ? Qt::LeftButton : Qt::NoButton);
}
inline void key(QWidget& w, Qt::Key k) {
    QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
    QApplication::sendEvent(&w, &e);
}
inline void leave(Viewport& w) {
    QEvent e(QEvent::Leave);
    QApplication::sendEvent(&w, &e);
}
} // namespace input_test
