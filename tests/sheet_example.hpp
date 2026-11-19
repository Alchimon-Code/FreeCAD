// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/drafting.hpp>
namespace sheet_test {
inline constexpr pd::Vec2 centre{1e7, -1e7};
inline void drawing(pd::Document& d) {
    using namespace pd;
    d.transact([&](Transaction& tx) {
        Layer ink("Witness");
        ink.color = {230, 20, 20};
        ink.lineWeight = .7;
        auto witness = tx.addLayer(ink);
        auto add = [&](auto e) {
            e.transform(Matrix3x3::translation(centre));
            tx.add(e);
        };
        Line ruler({-200, 0}, {200, 0});
        ruler.traits.layer = witness;
        add(ruler);
        Line tick({40, -10}, {40, 10});
        tick.traits.layer = witness;
        add(tick);
        Circle c({0, 0}, 3);
        c.traits.color = ByBlock{};
        const Entity* member = &c;
        auto pin = tx.defineBlock("Штифт", {&member, 1});
        BlockReference inside(pin, {2, 1}, {2, 2}, .45);
        inside.traits.color = ByBlock{};
        member = &inside;
        auto assembly = tx.defineBlock("Assembly", {&member, 1});
        BlockReference instance(assembly, {-22, 24}, {1, 2}, -.6);
        instance.traits.color = Color{10, 60, 190};
        add(instance);
        Polyline boundary({{{12, 14}, 0}, {{30, 14}, .4}, {{30, 33}, 0}, {{12, 33}, 0}}, true);
        Hatch h({boundary, Hatch::boundary(Circle({22, 23}, 4))}, HatchPattern::ANSI31, .8, .23);
        h.traits.color = Color{30, 80, 10};
        add(h);
        DimStyle style;
        style.precision = 3;
        style.arrow = DimStyle::Arrow::Tick;
        style.textHeight = 2.75;
        style.prefix = "L=";
        style.suffix = " mm";
        add(Dimension(DimKind::Aligned, {{-30, -20}, {10, -20}, {10, -28}}, style));
        add(Dimension(DimKind::Diameter, {{4, 14}, {7, 14}, {12, 12}}));
        add(Dimension(DimKind::Angular, {{55, -30}, {60, -30}, {55, -25}, {59, -26}}, DimStyle::eskd()));
        auto l = Layout::standard("Plate", Paper::A4, true, centre, .5);
        l.viewports = {ViewportEntity({{65, 70}, {165, 120}}, centre, .5)};
        l.title = "Корпус";
        l.designation = "ПД.01";
        tx.addLayout(l);
        auto detail = Layout::standard("Деталь", Paper::A3, false, centre, .1, false);
        tx.addLayout(detail);
    });
}
} // namespace sheet_test
