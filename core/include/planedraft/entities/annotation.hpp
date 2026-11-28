// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <array>
#include <planedraft/math/edges.hpp>

namespace pd {
// The derived geometry is immutable and shared by history versions. It has no model handles.
using Primitives = std::vector<std::shared_ptr<const Entity>>;
class SolidFace final : public Entity {
  public:
    SolidFace(Vec2 a, Vec2 b, Vec2 c);
    bool contains(Vec2) const;
    const std::array<Vec2, 3>& vertices() const { return points_; }
    BoundingBox2D getBounds() const override;
    void transform(const Matrix3x3&) override;
    std::unique_ptr<Entity> clone() const override { return std::make_unique<SolidFace>(*this); }
    std::vector<Vec2> getGripPoints() const override { return {points_.begin(), points_.end()}; }
    std::vector<Vec2> getSnapPoints(SnapType m) const override;

  private:
    std::array<Vec2, 3> points_;
};
enum class DimKind { Linear, Aligned, Radius, Diameter, Angular };
struct DimStyle {
    enum class Arrow { Filled, Tick };
    Arrow arrow = Arrow::Filled;
    double arrowSize = 2.5, textHeight = 2.5, extensionGap = .625, extensionOverrun = 1.25, textGap = .625;
    int precision = 2;
    std::string prefix, suffix;
    void validate() const;
    static DimStyle iso25() { return {}; }
    static DimStyle eskd();
    bool operator==(const DimStyle&) const = default;
};
class Dimension final : public Entity {
  public:
    // Linear/aligned: two measured points and a point on the dimension line. Radius/diameter:
    // centre, circumference and label. Angular: vertex, two ray points and a point on the desired arc.
    Dimension(DimKind, std::vector<Vec2>, DimStyle = DimStyle::iso25(), double linearAngle = 0);
    DimKind kind() const { return kind_; }
    const DimStyle& style() const { return style_; }
    double linearAngle() const { return angle_; }
    double measurement() const { return value_; } // angular: degrees
    const std::string& label() const { return label_; }
    const auto& defPoints() const { return pts_; }
    const Primitives& primitives() const { return parts_; }
    Dimension withPoint(size_t, Vec2) const;
    BoundingBox2D getBounds() const override { return bounds_; }
    void transform(const Matrix3x3&) override;
    std::unique_ptr<Entity> clone() const override { return std::make_unique<Dimension>(*this); }
    std::vector<Vec2> getGripPoints() const override { return pts_; }
    std::vector<Vec2> getSnapPoints(SnapType) const override;

  private:
    DimKind kind_;
    std::vector<Vec2> pts_;
    DimStyle style_;
    double angle_, value_ = 0;
    std::string label_;
    Primitives parts_;
    BoundingBox2D bounds_;
};
enum class HatchPattern { Solid, ANSI31, Grid, Concrete };
struct PatternFamily {
    double angle, spacing, dash = 0, gap = 0;
}; // radians, model units
class Hatch final : public Entity {
  public:
    // Even/odd boundary snapshots; no borrowed IDs. First vertex fixes the pattern phase on transforms.
    Hatch(std::vector<Polyline> loops, HatchPattern pattern = HatchPattern::ANSI31, double scale = 1,
          double angle = 0);
    static Polyline boundary(const Entity&); // closed polyline or circle (two semicircles)
    const auto& loops() const { return loops_; }
    auto pattern() const { return pattern_; }
    double scale() const { return scale_; }
    double angle() const { return angle_; }
    std::vector<PatternFamily> families() const;
    const Primitives& primitives() const { return parts_; }
    BoundingBox2D getBounds() const override { return bounds_; }
    void transform(const Matrix3x3&) override;
    std::unique_ptr<Entity> clone() const override { return std::make_unique<Hatch>(*this); }
    std::vector<Vec2> getGripPoints() const override { return {loops_.front().vertices().front().point}; }
    std::vector<Vec2> getSnapPoints(SnapType m) const override;

  private:
    std::vector<Polyline> loops_;
    HatchPattern pattern_;
    double scale_, angle_;
    Primitives parts_;
    BoundingBox2D bounds_;
};
// Chord error specified in local drawing units, including the closing bulge.
std::vector<Vec2> polygonize(const Polyline&, double tolerance);
} // namespace pd
