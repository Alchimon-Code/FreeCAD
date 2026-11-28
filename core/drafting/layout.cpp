// SPDX-License-Identifier: LGPL-2.1-or-later
#include <algorithm>
#include <charconv>
#include <cmath>
#include <planedraft/layout.hpp>
namespace pd {
ViewportEntity::ViewportEntity(BoundingBox2D box, Vec2 cam, double z) : box_(box), camera_(cam), scale_(z) {
    auto s = box.max() - box.min();
    if (!cam.finite() || !std::isfinite(z) || z <= 0 || s.x <= EPSILON || s.y <= EPSILON || !s.finite())
        throw std::invalid_argument("invalid paper viewport");
    (void)modelWindow();
}
BoundingBox2D ViewportEntity::modelWindow() const {
    auto half = (box_.max() - box_.min()) * (.5 / scale_);
    return {camera_ - half, camera_ + half};
}
Matrix3x3 ViewportEntity::modelToPaper() const {
    return Matrix3x3::translation(midpoint(box_.min(), box_.max())) * Matrix3x3::scaling(scale_, scale_) *
           Matrix3x3::translation(-camera_);
}
void ViewportEntity::transform(const Matrix3x3& m) {
    auto x = m.applyVector({1, 0}), y = m.applyVector({0, 1});
    if (x.x <= 0 || std::abs(x.y) > EPSILON || std::abs(y.x) > EPSILON || y.y <= 0)
        throw std::invalid_argument("paper viewport must stay axis aligned");
    double z = m.signedSimilarityScale();
    ViewportEntity v(m.applyBounds(box_), camera_, scale_ * z);
    box_ = v.box_;
    scale_ = v.scale_;
}
Vec2 Layout::size() const {
    const Vec2 formats[] = {{210, 297}, {297, 420}, {420, 594}, {594, 841}};
    auto n = static_cast<unsigned>(paper);
    if (n >= 4)
        throw std::invalid_argument("unknown paper format");
    auto s = formats[n];
    return landscape ? Vec2{s.y, s.x} : s;
}
BoundingBox2D Layout::printable() const {
    return {{margins.left, margins.bottom}, size() - Vec2{margins.right, margins.top}};
}
void Layout::validate() const {
    if (name.empty() || name == "Model" || name.find_first_of("\n\r") != name.npos)
        throw std::invalid_argument("invalid layout name");
    for (auto x : {margins.left, margins.bottom, margins.right, margins.top})
        if (!std::isfinite(x) || x < 0)
            throw std::invalid_argument("invalid paper margin");
    auto b = printable();
    auto s = b.max() - b.min();
    if (s.x <= 0 || s.y <= 0 || (stamp && (s.x < 185 || s.y <= 55)))
        throw std::invalid_argument("insufficient printable area");
    for (auto& p : paperEntities)
        if (!p || dynamic_cast<const ViewportEntity*>(p.get()))
            throw std::invalid_argument("invalid paper entity");
    for (auto& v : viewports)
        if (!BoundingBox2D({}, size()).contains(v.getBounds()))
            throw std::invalid_argument("viewport outside paper");
}
Layout Layout::standard(std::string name, Paper paper, bool landscape, Vec2 center, double scale,
                        bool stamp) {
    Layout l;
    l.name = std::move(name);
    l.paper = paper;
    l.landscape = landscape;
    l.stamp = stamp;
    auto b = l.printable();
    l.viewports.emplace_back(BoundingBox2D(b.min() + Vec2{3, stamp ? 58. : 3.}, b.max() - Vec2{3, 3}), center,
                             scale);
    l.validate();
    return l;
}
Primitives Layout::furniture(size_t sheetNumber, size_t sheetCount) const {
    validate();
    Primitives out;
    if (!frame)
        return out;
    auto b = printable();
    auto ll = b.min(), ur = b.max();
    Vec2 base{ur.x - 185, ll.y};
    auto line = [&](Vec2 a, Vec2 z, double w) {
        auto e = std::make_shared<Line>(a, z);
        e->traits.lineWeight = w;
        e->traits.color = Color{0, 0, 0};
        out.push_back(e);
    };
    line(ll, {ur.x, ll.y}, .7);
    line({ur.x, ll.y}, ur, .7);
    line(ur, {ll.x, ur.y}, .7);
    line({ll.x, ur.y}, ll, .7);
    if (!stamp)
        return out;
    auto h = [&](double x, double y, double x2, double w = .35) {
        line(base + Vec2{x, y}, base + Vec2{x2, y}, w);
    };
    auto v = [&](double x, double y, double y2, double w = .35) {
        line(base + Vec2{x, y}, base + Vec2{x, y2}, w);
    };
    auto text = [&](double x, double y, std::string str, double ht) {
        if (str.empty())
            return;
        size_t chars = 0;
        for (unsigned char c : str)
            chars += (c & 0xc0) != 0x80;
        auto t = std::make_shared<Text>(str, BoundingBox2D({0, 0}, {.6 * ht * chars, ht}),
                                        Matrix3x3::translation(base + Vec2{x, y}));
        t->traits.color = Color{0, 0, 0};
        out.push_back(t);
    };
    // 185 × 55 mm main inscription, form 1. Left: revisions/signatures; right: drawing data.
    h(0, 55, 185, .7);
    if (base.x > ll.x + EPSILON)
        v(0, 0, 55, .7);
    v(65, 0, 55, .7);
    for (auto x : {7, 17, 40, 55})
        v(x, x == 7 ? 30 : 0, 55);
    for (int y = 5; y < 55; y += 5)
        h(0, y, 65, y == 30 ? .7 : .25);
    h(65, 40, 185, .7);
    v(135, 0, 40, .7);
    h(65, 15, 135, .7);
    h(135, 35, 185);
    h(135, 20, 185, .7);
    h(135, 15, 185);
    v(150, 20, 40);
    v(167, 20, 40);
    v(158, 15, 20);
    text(1, 31, "Изм.", 1.6);
    text(8, 31, "Лист", 1.6);
    text(18, 31, "№ докум.", 1.6);
    text(41, 31, "Подп.", 1.6);
    text(56, 31, "Дата", 1.6);
    text(1, 26, "Разраб.", 2);
    text(1, 21, "Пров.", 2);
    text(1, 16, "Т.контр.", 2);
    text(1, 6, "Н.контр.", 2);
    text(1, 1, "Утв.", 2);
    text(136, 36, "Лит.", 2);
    text(151, 36, "Масса", 2);
    text(168, 36, "Масштаб", 2);
    text(136, 16, "Лист " + std::to_string(sheetNumber), 2);
    text(159, 16, "Листов " + std::to_string(sheetCount), 2);
    if (!viewports.empty() && std::all_of(viewports.begin(), viewports.end(), [&](auto& vp) {
            return vp.scale() == viewports.front().scale();
        })) {
        double scale = viewports.front().scale();
        char buffer[40];
        auto [end, err] = std::to_chars(buffer, buffer + sizeof(buffer), scale <= 1 ? 1 / scale : scale,
                                        std::chars_format::general, 6);
        if (err == std::errc{}) {
            std::string number(buffer, end);
            text(169, 25, scale <= 1 ? "1:" + number : number + ":1", 3);
        }
    }

    text(68, 45, designation, 4);
    text(68, 25, title, 3.5);
    return out;
}
} // namespace pd
