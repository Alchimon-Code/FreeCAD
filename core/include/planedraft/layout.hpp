// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/entities/annotation.hpp>
namespace pd {
enum class Paper { A4, A3, A2, A1 };
struct Margins {
    double left = 20, bottom = 5, right = 5, top = 5;
};
// Paper coordinates in mm, axes parallel to model XY. scale = paper mm / model mm.
class ViewportEntity final : public Entity {
  public:
    ViewportEntity(BoundingBox2D rectangle, Vec2 camera, double scale);
    Vec2 camera() const { return camera_; }
    double scale() const { return scale_; }
    Matrix3x3 modelToPaper() const;
    BoundingBox2D modelWindow() const;
    BoundingBox2D getBounds() const override { return box_; }
    std::vector<Vec2> getGripPoints() const override { return {box_.min(), box_.max()}; }
    std::vector<Vec2> getSnapPoints(SnapType m) const override {
        return hasSnap(m, SnapType::Endpoint) ? getGripPoints() : std::vector<Vec2>{};
    }
    std::unique_ptr<Entity> clone() const override { return std::make_unique<ViewportEntity>(*this); }
    void transform(const Matrix3x3&) override;

  private:
    BoundingBox2D box_;
    Vec2 camera_;
    double scale_;
};
struct Layout {
    std::string name = "Layout1";
    Paper paper = Paper::A4;
    bool landscape = true, stamp = true, frame = true;
    Margins margins;
    std::string designation, title;
    std::vector<ViewportEntity> viewports;
    Primitives paperEntities;
    Vec2 size() const;
    BoundingBox2D printable() const;
    void validate() const;
    Primitives furniture(size_t sheetNumber = 1, size_t sheetCount = 1) const;
    static Layout standard(std::string name, Paper, bool landscape, Vec2 camera, double scale,
                           bool stamp = true);
};
} // namespace pd
