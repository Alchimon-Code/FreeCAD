// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/entities/entity.hpp>
#include <planedraft/math/curves.hpp>

#include <span>

namespace pd {
class Line final : public Entity {
  public:
    Line(Vec2 start, Vec2 end);
    [[nodiscard]] Vec2 start() const { return start_; }
    [[nodiscard]] Vec2 end() const { return end_; }
    BoundingBox2D getBounds() const override;
    void transform(const Matrix3x3& mat) override;
    std::unique_ptr<Entity> clone() const override;
    std::vector<Vec2> getSnapPoints(SnapType mask) const override;
    std::vector<Vec2> getGripPoints() const override;

  private:
    Vec2 start_, end_;
};

class Circle final : public Entity {
  public:
    Circle(Vec2 center, double radius);
    [[nodiscard]] Vec2 center() const { return center_; }
    [[nodiscard]] double radius() const { return radius_; }
    BoundingBox2D getBounds() const override;
    void transform(const Matrix3x3& mat) override;
    std::unique_ptr<Entity> clone() const override;
    std::vector<Vec2> getSnapPoints(SnapType mask) const override;
    std::vector<Vec2> getGripPoints() const override;

  private:
    Vec2 center_;
    double radius_;
};

class Arc final : public Entity {
  public:
    explicit Arc(CircularArc curve) : curve_(curve) {}
    [[nodiscard]] const CircularArc& curve() const { return curve_; }
    BoundingBox2D getBounds() const override { return curve_.bounds(); }
    void transform(const Matrix3x3& mat) override { curve_ = curve_.transformed(mat); }
    std::unique_ptr<Entity> clone() const override;
    std::vector<Vec2> getSnapPoints(SnapType mask) const override;
    std::vector<Vec2> getGripPoints() const override;

  private:
    CircularArc curve_;
};

struct Vertex {
    Vec2 point;
    double bulge = 0;
};
class Polyline final : public Entity {
  public:
    Polyline(std::vector<Vertex> vertices, bool closed = false);
    [[nodiscard]] std::span<const Vertex> vertices() const { return vertices_; }
    [[nodiscard]] bool closed() const { return closed_; }
    BoundingBox2D getBounds() const override { return bounds_; }
    void transform(const Matrix3x3& mat) override;
    std::unique_ptr<Entity> clone() const override;
    std::vector<Vec2> getSnapPoints(SnapType mask) const override;
    std::vector<Vec2> getGripPoints() const override;

  private:
    std::vector<Vertex> vertices_;
    bool closed_;
    BoundingBox2D bounds_;
};

// Font-independent geometry: ink bounds are supplied by the text shaper, in local baseline coordinates.
class Text final : public Entity {
  public:
    Text(std::string utf8, BoundingBox2D localInk, Matrix3x3 placement);
    [[nodiscard]] const std::string& content() const { return text_; }
    [[nodiscard]] const Matrix3x3& placement() const { return placement_; }
    [[nodiscard]] BoundingBox2D localInk() const { return ink_; }
    BoundingBox2D getBounds() const override { return placement_.applyBounds(ink_); }
    void transform(const Matrix3x3& mat) override;
    std::unique_ptr<Entity> clone() const override;
    std::vector<Vec2> getSnapPoints(SnapType mask) const override;
    std::vector<Vec2> getGripPoints() const override;

  private:
    std::string text_;
    BoundingBox2D ink_;
    Matrix3x3 placement_;
};
} // namespace pd
