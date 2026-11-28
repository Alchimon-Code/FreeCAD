// SPDX-License-Identifier: LGPL-2.1-or-later
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <planedraft/entities/annotation.hpp>
#include <sstream>
namespace pd {
SolidFace::SolidFace(Vec2 a, Vec2 b, Vec2 c) : points_{a, b, c} { (void)getBounds(); }
BoundingBox2D SolidFace::getBounds() const {
    BoundingBox2D b;
    for (auto p : points_)
        b.include(p);
    return b;
}
bool SolidFace::contains(Vec2 p) const {
    auto [a, b, c] = points_;
    auto side = [](Vec2 a, Vec2 b, Vec2 p) { return (b - a).cross(p - a); };
    auto x = side(a, b, p), y = side(b, c, p), z = side(c, a, p);
    return ((x >= 0 && y >= 0 && z >= 0) || (x <= 0 && y <= 0 && z <= 0)) && side(a, b, c) != 0;
}
void SolidFace::transform(const Matrix3x3& m) {
    auto p = points_;
    for (auto& v : p) {
        v = m.applyPoint(v);
        if (!v.finite())
            throw std::invalid_argument("invalid face transform");
    }
    points_ = p;
}
std::vector<Vec2> SolidFace::getSnapPoints(SnapType m) const {
    return hasSnap(m, SnapType::Endpoint) ? getGripPoints() : std::vector<Vec2>{};
}
namespace {
constexpr double pi = std::numbers::pi;
double positive(double a) { return a < 0 ? a + 2 * pi : a; }
Vec2 direction(double a) { return {std::cos(a), std::sin(a)}; }
} // namespace
DimStyle DimStyle::eskd() {
    DimStyle s;
    s.arrowSize = s.textHeight = 3.5;
    s.extensionGap = s.textGap = 1;
    s.extensionOverrun = 2;
    s.precision = 1;
    return s;
}
void DimStyle::validate() const {
    if ((arrow != Arrow::Filled && arrow != Arrow::Tick) || precision < 0 || precision > 12 ||
        arrowSize <= EPSILON || textHeight <= EPSILON)
        throw std::invalid_argument("invalid dimension style");
    for (double x : {arrowSize, textHeight, extensionGap, extensionOverrun, textGap})
        if (!std::isfinite(x) || x < 0)
            throw std::invalid_argument("invalid dimension style");
}
Dimension::Dimension(DimKind k, std::vector<Vec2> p, DimStyle s, double axis)
    : kind_(k), pts_(std::move(p)), style_(std::move(s)), angle_(axis) {
    const auto& st = style_;
    if (k < DimKind::Linear || k > DimKind::Angular || pts_.size() != (k == DimKind::Angular ? 4u : 3u) ||
        !std::isfinite(axis))
        throw std::invalid_argument("invalid dimension parameters");
    st.validate();
    for (auto q : pts_)
        bounds_.include(q);
    auto add = [&](auto e) {
        bounds_.include(e.getBounds());
        parts_.push_back(std::make_shared<decltype(e)>(std::move(e)));
    };
    auto line = [&](Vec2 a, Vec2 b) {
        if ((b - a).length() > EPSILON)
            add(Line(a, b));
    };
    auto arrow = [&](Vec2 tip, Vec2 into) {
        auto u = into.normalized() * st.arrowSize, n = u.leftNormal();
        if (st.arrow == DimStyle::Arrow::Tick)
            line(tip - (u + n) * .5, tip + (u + n) * .5);
        else
            add(SolidFace(tip, tip + u + n * .17, tip + u - n * .17));
    };
    auto ext = [&](Vec2 source, Vec2 foot) {
        if ((foot - source).length() > EPSILON) {
            auto u = (foot - source).normalized();
            line(source + u * std::min(st.extensionGap, (foot - source).length()),
                 foot + u * st.extensionOverrun);
        }
    };
    auto a = pts_[0], b = pts_[1], c = pts_[2], u = (b - a).normalized();
    Vec2 anchor = c, baseline{1, 0};
    if (k == DimKind::Aligned || k == DimKind::Linear) {
        if (k == DimKind::Linear)
            u = direction(axis);
        double dist = (b - a).dot(u);
        if (dist < 0)
            u = -u;
        value_ = std::abs(dist);
        if (value_ <= EPSILON)
            throw std::invalid_argument("zero projected dimension");
        auto f = c + u * (a - c).dot(u), g = f + u * value_;
        ext(a, f);
        ext(b, g);
        line(f, g);
        arrow(f, u);
        arrow(g, -u);
        baseline = u;
        anchor = midpoint(f, g);
    } else if (k == DimKind::Radius || k == DimKind::Diameter) {
        value_ = (b - a).length() * (k == DimKind::Diameter ? 2 : 1);
        auto start = k == DimKind::Radius ? a : a - (b - a);
        line(start, b);
        arrow(b, -u);
        if (k == DimKind::Diameter)
            arrow(start, u);
        line(b, c);
    } else {
        auto v = (c - a).normalized();
        double start = std::atan2(u.y, u.x), sweep = positive(std::atan2(u.cross(v), u.dot(v)));
        auto w = pts_[3] - a;
        auto radius = w.length();
        if (positive(std::atan2(u.cross(w), u.dot(w))) > sweep + EPSILON)
            sweep -= 2 * pi;
        CircularArc arc(a, radius, start, sweep);
        add(Arc(arc));
        auto f = arc.pointAt(0), g = arc.pointAt(1);
        ext(b, f);
        ext(c, g);
        double sign = sweep > 0 ? 1 : -1;
        arrow(f, u.leftNormal() * sign);
        arrow(g, -v.leftNormal() * sign);
        value_ = std::abs(sweep) * 180 / pi;
        anchor = arc.pointAt(.5);
    }
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << (st.prefix.empty() ? (k == DimKind::Diameter ? "Ø"
                                  : k == DimKind::Radius ? "R"
                                                         : "")
                               : st.prefix)
         << std::fixed << std::setprecision(st.precision) << value_ << (k == DimKind::Angular ? "°" : "")
         << st.suffix;
    label_ = text.str();
    if (baseline.x < 0 || (std::abs(baseline.x) < EPSILON && baseline.y < 0))
        baseline = -baseline;
    auto chars =
        std::count_if(label_.begin(), label_.end(), [](unsigned char ch) { return (ch & 0xc0) != 0x80; });
    double width = .6 * st.textHeight * chars;
    add(Text(label_, {{0, 0}, {width, st.textHeight}},
             Matrix3x3::translation(anchor - baseline * (width / 2) + baseline.leftNormal() * st.textGap) *
                 Matrix3x3::rotation(std::atan2(baseline.y, baseline.x))));
}
Dimension Dimension::withPoint(size_t i, Vec2 v) const {
    auto p = pts_;
    p.at(i) = v;
    auto result = Dimension(kind_, p, style_, angle_);
    result.traits = traits;
    return result;
}
void Dimension::transform(const Matrix3x3& m) {
    auto z = std::abs(m.signedSimilarityScale());
    auto p = pts_;
    for (auto& v : p)
        v = m.applyPoint(v);
    auto st = style_;
    for (auto v : {&st.textHeight, &st.arrowSize, &st.extensionGap, &st.extensionOverrun, &st.textGap})
        *v *= z;
    auto u = m.applyVector(direction(angle_));
    Dimension result(kind_, p, st, std::atan2(u.y, u.x));
    result.traits = traits; // Keep the published object's identity when used by Transaction::transform.
    pts_.swap(result.pts_);
    style_ = std::move(result.style_);
    angle_ = result.angle_;
    value_ = result.value_;
    label_.swap(result.label_);
    parts_.swap(result.parts_);
    bounds_ = result.bounds_;
}
std::vector<Vec2> Dimension::getSnapPoints(SnapType m) const {
    return hasSnap(m, SnapType::Endpoint) ? pts_ : std::vector<Vec2>{};
}
} // namespace pd
