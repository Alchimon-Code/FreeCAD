// SPDX-License-Identifier: LGPL-2.1-or-later
#include "camera2d.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pd {
void Camera2D::resize(double w, double h) { size_ = {std::max(1.0, w), std::max(1.0, h)}; }
void Camera2D::setView(Vec2 eye, double scale) {
    if (!eye.finite() || !std::isfinite(scale) || scale <= 0) {
        throw std::invalid_argument("invalid camera");
    }
    eye_ = eye;
    scale_ = std::clamp(scale, minScale, maxScale);
}
Vec2 Camera2D::worldToScreen(Vec2 p) const {
    const auto d = (p - eye_) * scale_;
    return size_ * 0.5 + Vec2{d.x, -d.y};
}
Vec2 Camera2D::screenToWorld(Vec2 s) const {
    const auto d = (s - size_ * 0.5) / scale_;
    return eye_ + Vec2{d.x, -d.y};
}
void Camera2D::pan(Vec2 d) { eye_ = eye_ + Vec2{-d.x, d.y} / scale_; }
void Camera2D::zoomAt(Vec2 s, double factor) {
    if (!(factor > 0)) {
        return;
    }
    const double next = std::clamp(scale_ * factor, minScale, maxScale);
    const auto d = (s - size_ * 0.5) * (1 / scale_ - 1 / next);
    eye_ = eye_ + Vec2{d.x, -d.y};
    scale_ = next;
}
bool Camera2D::fit(const BoundingBox2D& box) {
    if (box.empty()) {
        return false;
    }
    const auto center = midpoint(box.min(), box.max());
    const auto half = box.max() * 0.5 - box.min() * 0.5;
    double scale = maxScale;
    if (half.x > 0) {
        scale = std::min(scale, 0.45 * size_.x / half.x);
    }
    if (half.y > 0) {
        scale = std::min(scale, 0.45 * size_.y / half.y);
    }
    setView(center, half == Vec2{} ? scale_ : scale);
    return true;
}
BoundingBox2D Camera2D::viewBounds(double pad) const {
    const auto half = (size_ * 0.5 + Vec2{pad, pad}) / scale_;
    return {eye_ - half, eye_ + half};
}
std::array<float, 2> Camera2D::relativeToEye(Vec2 world, Vec2 uploadEye) {
    auto d = world - uploadEye;
    return {static_cast<float>(d.x), static_cast<float>(d.y)};
}
std::array<float, 9> Camera2D::clipMatrix(Vec2 origin) const {
    const auto d = origin - eye_;
    const double sx = 2 * scale_ / size_.x, sy = 2 * scale_ / size_.y;
    return {static_cast<float>(sx),       0, 0, 0, static_cast<float>(sy), 0, static_cast<float>(d.x * sx),
            static_cast<float>(d.y * sy), 1};
}
} // namespace pd
