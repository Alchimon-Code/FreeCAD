// SPDX-License-Identifier: LGPL-2.1-or-later
#include "dxf_common.hpp"
#include <algorithm>
#include <cmath>
#include <planedraft/drafting.hpp>
#include <set>
namespace pd::io {
namespace {
using namespace dxf;
class Writer {
    std::ostream& out;
    DxfVersion version;
    bool ascii;

  public:
    Writer(std::ostream& stream, DxfVersion v, bool asciiOnly)
        : out(stream), version(v), ascii(asciiOnly) {}

  private:
    std::string str(std::string_view s) const { return ascii ? encodeAscii(s) : encode(s); }
    void colorCode(Color c, bool visible = true) {
        g(62, closestAci(c) * (visible ? 1 : -1));
        if (version == DxfVersion::R2004)
            g(420, (c.r << 16) | (c.g << 8) | c.b);
    }
    unsigned long long next = 0x100;
    size_t groups = 0;
    void g(int code, std::string_view value) {
        if (code == 0)
            groups = 0;
        else if (++groups > maxRecordGroups)
            throw Failure(IoStatus::Unsupported, "DXF record too large");
        if (value.size() > maxLineBytes)
            throw Failure(IoStatus::Unsupported, "DXF string too long");
        out << code << '\n' << value << '\n';
    }
    void g(int code, int value) { g(code, std::to_string(value)); }
    void g(int code, double value) { g(code, num(value)); }
    void at(int code, Vec2 p) {
        g(code, p.x);
        g(code + 10, p.y);
        g(code + 20, 0);
    }
    void section(const char* name) {
        g(0, "SECTION");
        g(2, name);
    }
    void end() { g(0, "ENDSEC"); }
    void record(const char* type, std::string_view owner, std::string handle = {}) {
        g(0, type);
        g(5, handle.empty() ? hex(next++) : handle);
        g(330, owner);
    }
    void table(const char* name, const char* handle, int n) {
        g(0, "TABLE");
        g(2, name);
        g(5, handle);
        g(330, "0");
        g(100, "AcDbSymbolTable");
        g(70, n);
    }
    void row(const char* type, const char* owner, const char* subclass, std::string handle = {}) {
        record(type, owner, std::move(handle));
        g(100, "AcDbSymbolTableRecord");
        g(100, subclass);
    }
    void symbol(int code, const std::string& name) {
        if (name.empty() || name.find_first_of("<>/\\\":;?*|=,") != name.npos || unicode(name).size() > 255)
            throw Failure(IoStatus::Unsupported, "invalid DXF symbol name: " + name);
        g(code, str(name));
    }
    void extras(const Color* c, const double* w, const Text* t) {
        bool rgb = version == DxfVersion::R2000 && c && aci(closestAci(*c)) != *c,
             weight = w && weightCode(*w) * .01 != *w;
        if (!rgb && !weight && !t)
            return;
        g(1001, "PLANEDRAFT");
        if (rgb) {
            g(1000, "RGB");
            g(1071, (c->r << 16) | (c->g << 8) | c->b);
        }
        if (weight) {
            g(1000, "WEIGHT");
            g(1040, *w);
        }
        if (t) {
            g(1000, "INK");
            auto b = t->localInk();
            for (double n : {b.min().x, b.min().y, b.max().x, b.max().y})
                g(1040, n);
        }
    }
    void label(const Text& t) {
        auto m = t.placement();
        auto x = m.applyVector({1, 0}), y = m.applyVector({0, 1});
        auto unit = x / x.length();
        double sy = unit.cross(y);
        double height = (t.localInk().max().y - t.localInk().min().y) * std::abs(sy);
        if (!(height > 0) || sy == 0)
            throw Failure(IoStatus::Unsupported, "TEXT has degenerate ink metrics");
        g(100, "AcDbText");
        at(10, m.applyPoint({}));
        g(40, height);
        g(1, str(t.content()));
        g(50, std::atan2(unit.y, unit.x) / rad);
        g(41, x.length() / std::abs(sy));
        g(51, std::atan(unit.dot(y) / sy) / rad);
        g(71, sy < 0 ? 4 : 0);
        g(7, "Standard");
        g(100, "AcDbText");
    }
    struct BlockRow {
        std::string name, handle;
        const BlockDefinition* block = nullptr;
        const Dimension* dim = nullptr;
        const Layout* paper = nullptr;
        std::string layoutHandle;
    };
    std::vector<BlockRow> blocks;
    std::map<const Dimension*, size_t> dimRows;
    std::vector<DimStyle> styles;
    void insert(const BlockReference& r) {
        auto& m = r.placement();
        auto x = m.applyVector({1, 0}), y = m.applyVector({0, 1});
        auto u = x.normalized();
        if (std::abs(u.dot(y.normalized())) > EPSILON)
            throw Failure(IoStatus::Unsupported, "DXF INSERT cannot represent shear");
        g(100, "AcDbBlockReference");
        symbol(2, r.definition().name());
        at(10, m.applyPoint({}));
        g(41, x.length());
        g(42, u.cross(y));
        g(43, 1);
        g(50, std::atan2(u.y, u.x) / rad);
    }
    void hatch(const Hatch& h) {
        g(100, "AcDbHatch");
        at(10, {});
        atNormal(1);
        const char* names[] = {"SOLID", "ANSI31", "GRID", "PD_CONCRETE"};
        g(2, names[int(h.pattern())]);
        g(70, h.pattern() == HatchPattern::Solid ? 1 : 0);
        g(71, 0);
        g(91, int(h.loops().size()));
        bool first = true;
        for (auto& loop : h.loops()) {
            g(92, first ? 3 : 2);
            first = false;
            g(72, 1);
            g(73, 1);
            g(93, int(loop.vertices().size()));
            for (auto v : loop.vertices()) {
                g(10, v.point.x);
                g(20, v.point.y);
                g(42, v.bulge);
            }
            g(97, 0);
        }
        g(75, 0);
        g(76, 1);
        auto families = h.families();
        if (!families.empty()) {
            g(52, h.angle() / rad);
            g(41, h.scale());
            g(77, 0);
            g(78, int(families.size()));
            auto origin = h.loops()[0].vertices()[0].point;
            for (auto f : families) {
                g(53, f.angle / rad);
                g(43, origin.x);
                g(44, origin.y);
                g(45, -std::sin(f.angle) * f.spacing);
                g(46, std::cos(f.angle) * f.spacing);
                g(79, f.dash ? 2 : 0);
                if (f.dash) {
                    g(49, f.dash);
                    g(49, -f.gap);
                }
            }
        }
        g(98, 0);
    }
    std::string styleName(size_t i) const {
        if (i == 0)
            return "ISO-25";
        return styles[i] == DimStyle::eskd() ? "ESKD" : "PD_DIM_" + std::to_string(i);
    }
    void dimension(const Dimension& d) {
        auto& p = d.defPoints();
        auto k = d.kind();
        g(100, "AcDbDimension");
        g(2, blocks.at(dimRows.at(&d)).name);
        g(3, styleName(std::find(styles.begin(), styles.end(), d.style()) - styles.begin()));
        bool radial = k == DimKind::Radius || k == DimKind::Diameter;
        at(10, radial ? (k == DimKind::Radius ? p[0] : p[0] - (p[1] - p[0])) : p.back());
        auto labelBounds = d.primitives().back()->getBounds();
        at(11, radial ? p[2] : midpoint(labelBounds.min(), labelBounds.max()));
        const int types[] = {0, 1, 4, 3, 5};
        g(70, 32 + types[int(k)]);
        g(71, 5);
        g(1, "<>");
        if (radial) {
            g(100, k == DimKind::Radius ? "AcDbRadialDimension" : "AcDbDiametricDimension");
            at(15, p[1]);
            g(40, 0);
        } else if (k == DimKind::Angular) {
            g(100, "AcDb3PointAngularDimension");
            at(13, p[1]);
            at(14, p[2]);
            at(15, p[0]);
        } else {
            g(100, "AcDbAlignedDimension");
            at(13, p[0]);
            at(14, p[1]);
            if (k == DimKind::Linear) {
                g(50, d.linearAngle() / rad);
                g(100, "AcDbRotatedDimension");
            }
        }
    }
    void dimstyles() {
        if (dimRows.empty())
            return;
        table("DIMSTYLE", "8", int(styles.size()));
        g(100, "AcDbDimStyleTable");
        g(71, 0);
        for (size_t i = 0; i < styles.size(); ++i) {
            auto& s = styles[i];
            g(0, "DIMSTYLE");
            g(105, hex(next++));
            g(330, "8");
            g(100, "AcDbSymbolTableRecord");
            g(100, "AcDbDimStyleTableRecord");
            g(2, styleName(i));
            g(70, 0);
            g(3, str(s.prefix + "<>" + s.suffix));
            g(40, 1);
            g(41, s.arrowSize);
            g(42, s.extensionGap);
            g(44, s.extensionOverrun);
            g(140, s.textHeight);
            g(142, s.arrow == DimStyle::Arrow::Tick ? s.arrowSize : 0);
            g(147, s.textGap);
            g(271, s.precision);
            g(275, 0);
            g(277, 2);
        }
        g(0, "ENDTAB");
    }
    void viewport(const ViewportEntity& v, const BlockRow& b, int id) {
        auto box = v.getBounds();
        auto s = box.max() - box.min();
        record("VIEWPORT", b.handle);
        g(100, "AcDbEntity");
        g(67, 1);
        g(8, "0");
        g(100, "AcDbViewport");
        at(10, midpoint(box.min(), box.max()));
        g(40, s.x);
        g(41, s.y);
        g(68, id);
        g(69, id);
        g(12, v.camera().x);
        g(22, v.camera().y);
        g(16, 0);
        g(26, 0);
        g(36, 1);
        g(17, 0);
        g(27, 0);
        g(37, 0);
        g(45, s.y / v.scale());
        g(90, 0);
    }
    void layout(const BlockRow& b, int index) {
        Layout model;
        auto& l = b.paper ? *b.paper : model;
        auto s = l.size();
        record("LAYOUT", "D", b.layoutHandle);
        g(100, "AcDbPlotSettings");
        g(1, "");
        g(4, "ISO");
        g(40, l.margins.left);
        g(41, l.margins.bottom);
        g(42, l.margins.right);
        g(43, l.margins.top);
        g(44, s.x);
        g(45, s.y);
        g(142, 1);
        g(143, 1);
        g(70, b.paper ? 0 : 1024);
        g(72, 1);
        g(73, 0);
        g(74, 5);
        g(100, "AcDbLayout");
        g(1, b.paper ? str(l.name) : "Model");
        g(70, 1);
        g(71, index);
        g(10, 0);
        g(20, 0);
        g(11, s.x);
        g(21, s.y);
        at(12, {});
        at(14, {});
        at(15, s);
        at(16, {1, 0});
        at(17, {0, 1});
        g(330, b.handle);
        if (b.paper) {
            g(1001, "PLANEDRAFT");
            g(1000, "SHEET");
            g(1070, l.frame ? 1 : 0);
            g(1070, l.stamp ? 1 : 0);
            g(1000, str(l.designation));
            g(1000, str(l.title));
        }
    }
    void entity(const Entity& e, const Document& doc, const std::string& owner = "6", bool paper = false,
                bool furniture = false) {
        const auto* l = dynamic_cast<const Line*>(&e);
        auto* c = dynamic_cast<const Circle*>(&e);
        auto* a = dynamic_cast<const Arc*>(&e);
        auto* p = dynamic_cast<const Polyline*>(&e);
        auto* t = dynamic_cast<const Text*>(&e);
        auto ins = dynamic_cast<const BlockReference*>(&e);
        auto h = dynamic_cast<const Hatch*>(&e);
        auto dim = dynamic_cast<const Dimension*>(&e);
        auto face = dynamic_cast<const SolidFace*>(&e);
        auto type = l      ? "LINE"
                    : c    ? "CIRCLE"
                    : a    ? "ARC"
                    : p    ? "LWPOLYLINE"
                    : t    ? "TEXT"
                    : ins  ? "INSERT"
                    : h    ? "HATCH"
                    : dim  ? "DIMENSION"
                    : face ? "SOLID"
                           : nullptr;
        if (!type)
            throw Failure(IoStatus::Unsupported, "unsupported entity; drawing was not saved");
        record(type, owner);
        g(100, "AcDbEntity");
        if (paper)
            g(67, 1);
        symbol(8, doc.layer(e.traits.layer)->name);
        const auto* rgb = std::get_if<Color>(&e.traits.color);
        const auto* w = std::get_if<double>(&e.traits.lineWeight);
        if (rgb)
            colorCode(*rgb);
        else if (std::holds_alternative<ByBlock>(e.traits.color))
            g(62, 0);
        if (w)
            g(370, weightCode(*w));
        else if (std::holds_alternative<ByBlock>(e.traits.lineWeight))
            g(370, -2);
        if (auto lt = std::get_if<std::string>(&e.traits.lineType))
            symbol(6, *lt);
        else if (std::holds_alternative<ByBlock>(e.traits.lineType))
            g(6, "BYBLOCK");
        if (!e.traits.isVisible)
            g(60, 1);
        if (l) {
            g(100, "AcDbLine");
            at(10, l->start());
            at(11, l->end());
        } else if (c) {
            g(100, "AcDbCircle");
            at(10, c->center());
            g(40, c->radius());
        } else if (a) {
            const auto& arc = a->curve();
            bool flipped = arc.sweep() < 0;
            auto center = arc.center();
            if (flipped)
                center.x = -center.x;
            double start = flipped ? 180 - arc.startAngle() / rad : arc.startAngle() / rad;
            g(100, "AcDbCircle");
            at(10, center);
            g(40, arc.radius());
            if (flipped)
                atNormal(-1);
            g(100, "AcDbArc");
            g(50, start);
            g(51, start + std::abs(arc.sweep()) / rad);
        } else if (p) {
            g(100, "AcDbPolyline");
            g(90, static_cast<int>(p->vertices().size()));
            g(70, p->closed() ? 1 : 0);
            for (auto v : p->vertices()) {
                g(10, v.point.x);
                g(20, v.point.y);
                if (v.bulge)
                    g(42, v.bulge);
            }
        } else if (t)
            label(*t);
        else if (ins)
            insert(*ins);
        else if (h)
            hatch(*h);
        else if (dim)
            dimension(*dim);
        else {
            g(100, "AcDbTrace");
            for (int i = 0; i < 4; ++i)
                at(10 + i, face->vertices()[std::min(i, 2)]);
        }

        extras(rgb, w, t);
        if (furniture) {
            if (!t && (!rgb || version != DxfVersion::R2000 || aci(closestAci(*rgb)) == *rgb) &&
                (!w || weightCode(*w) * .01 == *w))
                g(1001, "PLANEDRAFT");
            g(1000, "FURNITURE");
            g(1070, 1);
        }
    }
    void atNormal(int z) {
        g(210, 0);
        g(220, 0);
        g(230, z);
    }

  public:
    Writer(std::ostream& o, DxfVersion v) : out(o), version(v) {}
    void drawing(const Document& d) {
        auto ids = d.entityIds(), layers = d.layerIds(), sheets = d.layoutIds();
        bool hasSheets = !sheets.empty();
        blocks.push_back({"*Model_Space", "6", nullptr, nullptr, nullptr, hasSheets ? hex(next++) : ""});
        blocks.push_back({"*Paper_Space", "7", nullptr, nullptr,
                          hasSheets ? d.layout(sheets.front()) : nullptr, hasSheets ? hex(next++) : ""});
        for (size_t i = 1; i < sheets.size(); ++i)
            blocks.push_back({"*Paper_Space" + std::to_string(i - 1), hex(next++), nullptr, nullptr,
                              d.layout(sheets[i]), hex(next++)});
        if (ids.size() > maxEntities || layers.size() > maxLayers)
            throw Failure(IoStatus::Unsupported, "drawing exceeds DXF exchange limits");
        if (d.layer(0)->name != "0")
            throw Failure(IoStatus::Unsupported, "DXF requires default layer 0");
        std::map<std::string, std::string> types;
        std::set<std::string> layerNames;
        auto lt = [&](const std::string& s) { types.try_emplace(symbolKey(s), s); };
        lt("ByBlock");
        lt("ByLayer");
        lt("Continuous");
        for (auto id : layers) {
            auto& l = *d.layer(id);
            lt(l.lineType);
            if (!layerNames.insert(symbolKey(l.name)).second)
                throw Failure(IoStatus::Unsupported, "layer names collide in DXF");
        }
        std::set<std::string> sheetNames{"MODEL"};
        for (auto id : sheets)
            if (!sheetNames.insert(symbolKey(d.layout(id)->name)).second)
                throw Failure(IoStatus::Unsupported, "layout names collide in DXF");
        styles.push_back(DimStyle::iso25());
        auto collect = [&](const Entity& e) {
            if (auto s = std::get_if<std::string>(&e.traits.lineType))
                lt(*s);
            if (auto d = dynamic_cast<const Dimension*>(&e)) {
                if (dimRows.contains(d))
                    return;
                dimRows[d] = blocks.size();
                blocks.push_back(
                    {"*D" + std::to_string(dimRows.size()), hex(next++), nullptr, d, nullptr, {}});
                if (std::find(styles.begin(), styles.end(), d->style()) == styles.end())
                    styles.push_back(d->style());
            }
        };
        std::set<std::string> names;
        for (auto id : d.blockIds()) {
            auto def = d.block(id);
            if (!names.insert(symbolKey(def->name())).second || def->name().starts_with('*'))
                throw Failure(IoStatus::Unsupported, "invalid or duplicate block name");
            blocks.push_back({def->name(), hex(next++), def.get(), nullptr, nullptr, {}});
            for (auto& e : def->members())
                collect(*e);
        }
        for (auto id : ids)
            collect(*d.entity(id));
        for (auto id : sheets)
            for (auto& e : d.layout(id)->paperEntities)
                collect(*e);
        section("HEADER");
        g(9, "$ACADVER");
        g(1, version == DxfVersion::R2000 ? "AC1015" : "AC1018");
        g(9, "$DWGCODEPAGE");
        g(3, "ANSI_1251");
        g(9, "$INSUNITS");
        g(70, 4);
        g(9, "$HANDSEED");
        out << "5\n";
        auto seed = out.tellp();
        out << "0000000000000000\n";
        if (auto b = d.extents(); !b.empty()) {
            g(9, "$EXTMIN");
            at(10, b.min());
            g(9, "$EXTMAX");
            at(10, b.max());
        }
        end();
        section("TABLES");
        table("LTYPE", "1", static_cast<int>(types.size()));
        for (auto& [_, name] : types) {
            row("LTYPE", "1", "AcDbLinetypeTableRecord");
            symbol(2, name);
            g(70, 0);
            g(3, "");
            g(72, 65);
            // A drawing that came in with a dash pattern goes out with the same one, or the reader of
            // this file would draw a dashed line solid.
            const auto pattern = d.lineTypePattern(name);
            if (pattern.empty()) {
                g(73, 0);
                g(40, 0);
                continue;
            }
            double total = 0;
            for (double value : pattern)
                total += std::abs(value);
            g(73, static_cast<int>(pattern.size()));
            g(40, total);
            for (double value : pattern)
                g(49, value);
            g(74, 0);
        }
        g(0, "ENDTAB");
        table("LAYER", "2", static_cast<int>(layers.size()));
        for (auto id : layers) {
            auto& l = *d.layer(id);
            row("LAYER", "2", "AcDbLayerTableRecord");
            symbol(2, l.name);
            g(70, (l.isFrozen ? 1 : 0) | (l.isLocked ? 4 : 0));
            colorCode(l.color, l.isVisible);
            symbol(6, l.lineType);
            g(370, weightCode(l.lineWeight));
            extras(&l.color, &l.lineWeight, nullptr);
        }
        g(0, "ENDTAB");
        table("STYLE", "3", 1);
        row("STYLE", "3", "AcDbTextStyleTableRecord");
        g(2, "Standard");
        g(70, 0);
        g(40, 0);
        g(41, 1);
        g(50, 0);
        g(71, 0);
        g(42, 1);
        g(3, "txt");
        g(4, "");
        g(0, "ENDTAB");
        table("APPID", "4", 2);
        for (auto name : {"ACAD", "PLANEDRAFT"}) {
            row("APPID", "4", "AcDbRegAppTableRecord");
            g(2, name);
            g(70, 0);
        }
        g(0, "ENDTAB");
        dimstyles();
        table("BLOCK_RECORD", "5", int(blocks.size()));
        for (auto& b : blocks) {
            row("BLOCK_RECORD", "5", "AcDbBlockTableRecord", b.handle);
            g(2, str(b.name));
            if (!b.layoutHandle.empty())
                g(340, b.layoutHandle);
        }
        g(0, "ENDTAB");
        end();
        section("BLOCKS");
        auto paperContents = [&](const BlockRow& b, size_t sheetNumber) {
            auto& l = *b.paper;
            auto size = l.size();
            viewport(ViewportEntity({{}, size}, size * .5, 1), b, 1);
            int i = 1;
            for (auto& v : l.viewports)
                viewport(v, b, ++i);
            for (auto& e : l.furniture(sheetNumber, sheets.size()))
                entity(*e, d, b.handle, true, true);
            for (auto& e : l.paperEntities)
                entity(*e, d, b.handle, true);
        };
        size_t paperNumber = 1; // active paper (block 1) is written in ENTITIES
        for (auto& b : blocks) {
            auto owner = b.handle, name = str(b.name);
            record("BLOCK", owner);
            g(100, "AcDbEntity");
            g(8, "0");
            g(100, "AcDbBlockBegin");
            g(2, name);
            g(70, b.dim ? 1 : 0);
            at(10, {});
            g(3, name);
            g(1, "");
            if (b.block)
                for (auto& e : b.block->members())
                    entity(*e, d, owner);
            if (b.dim)
                for (auto& e : b.dim->primitives()) {
                    auto copy = e->clone();
                    copy->traits.color = ByBlock{};
                    copy->traits.lineWeight = ByBlock{};
                    entity(*copy, d, owner);
                }
            if (b.paper && owner != "7")
                paperContents(b, ++paperNumber);
            record("ENDBLK", owner);
            g(100, "AcDbEntity");
            g(8, "0");
            g(100, "AcDbBlockEnd");
        }
        end();
        section("ENTITIES");
        for (auto id : ids)
            entity(*d.entity(id), d);
        if (hasSheets)
            paperContents(blocks[1], 1);
        end();
        section("OBJECTS");
        record("DICTIONARY", "0", "C");
        g(100, "AcDbDictionary");
        g(281, 1);
        if (hasSheets) {
            g(3, "ACAD_LAYOUT");
            g(350, "D");
            record("DICTIONARY", "C", "D");
            g(100, "AcDbDictionary");
            g(281, 1);
            for (auto& b : blocks)
                if (!b.layoutHandle.empty()) {
                    g(3, b.paper ? str(b.paper->name) : "Model");
                    g(350, b.layoutHandle);
                }
            int index = 0;
            for (auto& b : blocks)
                if (!b.layoutHandle.empty())
                    layout(b, index++);
        }
        end();
        g(0, "EOF");
        auto finish = out.tellp();
        out.seekp(seed);
        out << hex(next, 16);
        out.seekp(finish);
    }
};
} // namespace
IoResult DxfHandler::write(const fs::path& p, const Document& d) const {
    return checked([&](IoResult&) {
        auto f = output(p);
        Writer(f, version_, ascii_).drawing(d);
        f.close();
        if (fs::file_size(p) > maxFileBytes)
            throw Failure(IoStatus::Unsupported, "DXF output exceeds size limit");
    });
}
} // namespace pd::io
