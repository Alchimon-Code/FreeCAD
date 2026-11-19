// SPDX-License-Identifier: LGPL-2.1-or-later
#include "dxf_common.hpp"
#include <cmath>
#include <numeric>
#include <optional>
#include <planedraft/drafting.hpp>
#include <set>
namespace pd::io {
namespace {
using namespace dxf;
struct Group {
    int code;
    std::string value;
    size_t line;
};
struct Record {
    std::string type;
    size_t line = 0;
    std::vector<Group> groups, extra;
    size_t foreignLine = 0; // loss of opaque per-record metadata, not a skipped entity
    void warnIfPartial(IoResult& r) const {
        if (foreignLine)
            r.warn(foreignLine, type, "nonstandard or application-specific groups not imported");
    }
    const Group* get(int code) const {
        for (auto& g : groups)
            if (g.code == code)
                return &g;
        return nullptr;
    }
    std::string str(int c, std::string fallback = {}) const {
        auto* g = get(c);
        return g ? g->value : std::move(fallback);
    }
    int i(int c, int fallback = 0) const {
        auto* g = get(c);
        return g ? integer(g->value) : fallback;
    }
    double n(int c, double fallback = 0) const {
        auto* g = get(c);
        return g ? real(g->value) : fallback;
    }
    double required(int c) const {
        auto* g = get(c);
        if (!g)
            throw std::invalid_argument("missing group " + std::to_string(c));
        return real(g->value);
    }
    Vec2 xy(int c) const { return {required(c), required(c + 10)}; }
    std::span<const Group> field(std::string_view name) const {
        for (size_t j = 0; j < extra.size(); ++j)
            if (extra[j].code == 1000 && extra[j].value == name)
                return std::span(extra).subspan(j + 1);
        return {};
    }
};
class Records {
  public:
    explicit Records(const fs::path& file) {
        if (!fs::is_regular_file(file))
            throw Failure(IoStatus::FileError, "cannot open DXF");
        if (fs::file_size(file) > maxFileBytes)
            throw Failure(IoStatus::InvalidData, "DXF exceeds size limit");
        stream_.open(file, std::ios::binary);
        if (!stream_)
            throw Failure(IoStatus::FileError, "cannot open DXF");
        next_ = pair();
    }
    std::optional<Record> next() {
        if (!next_)
            return {};
        auto start = std::move(*next_);
        if (start.code != 0)
            throw Failure(IoStatus::InvalidData, "expected group 0", start.line);
        Record r{upper(trim(start.value)), start.line, {}, {}};
        bool pd = false;
        int braces = 0;
        while ((next_ = pair()) && next_->code != 0) {
            auto& g = *next_;
            if (!r.foreignLine &&
                (g.code < 0 || g.code > 1071 || g.code == 102 || (g.code == 1001 && g.value != "PLANEDRAFT")))
                r.foreignLine = g.line;
            if (g.code < 0 || g.code > 1071)
                continue;
            if (g.code == 102) {
                if (g.value.starts_with('{'))
                    ++braces;
                else if (g.value == "}" && braces)
                    --braces;
                continue;
            }
            if (braces)
                continue;
            if (g.code == 1001) {
                pd = g.value == "PLANEDRAFT";
                continue;
            }
            if (g.code >= 1000) {
                if (pd)
                    r.extra.push_back(std::move(g));
            } else
                r.groups.push_back(std::move(g));
            if (r.groups.size() + r.extra.size() > maxRecordGroups)
                throw Failure(IoStatus::InvalidData, "DXF record too large", r.line, r.type);
        }
        return r;
    }

  private:
    std::ifstream stream_;
    size_t line_ = 0, bytes_ = 0;
    std::optional<Group> next_;
    bool line(std::string& s) {
        if (!std::getline(stream_, s)) {
            if (stream_.bad())
                throw Failure(IoStatus::FileError, "DXF read failed", line_);
            return false;
        }
        ++line_;
        bytes_ += s.size() + 1;
        if (bytes_ > maxFileBytes + 1 || s.size() > maxLineBytes)
            throw Failure(IoStatus::InvalidData, "DXF input limit exceeded", line_);
        if (s.ends_with('\r'))
            s.pop_back();
        return true;
    }
    std::optional<Group> pair() {
        std::string k, v;
        while (line(k)) {
            if (line_ == 1 && k.starts_with("\xef\xbb\xbf"))
                k.erase(0, 3);
            if (trim(k).empty())
                continue;
            int code;
            try {
                code = integer(k);
            } catch (...) {
                throw Failure(IoStatus::InvalidData, "invalid ASCII group code", line_);
            }
            if (!line(v))
                throw Failure(IoStatus::InvalidData, "invalid or truncated group", line_);
            if (code != 999)
                return Group{code, std::move(v), line_ - 1};
        }
        return {};
    }
};
Color rgb(int n) {
    if (n < 0 || n > 0xffffff)
        throw std::invalid_argument("invalid RGB color");
    return {(uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n};
}
Color color(const Record& r, int index) {
    if (r.get(420))
        return rgb(r.i(420));
    auto e = r.field("RGB");
    if (!e.empty() && e[0].code == 1071) {
        auto c = rgb(integer(e[0].value));
        if (closestAci(c) == index)
            return c;
    }
    return aci(index);
}
double weight(const Record& r, int w) {
    auto e = r.field("WEIGHT");
    if (!e.empty() && e[0].code == 1040) {
        auto mm = real(e[0].value);
        if (mm >= 0 && weightCode(mm) == w)
            return mm;
    }
    return w < 0 ? .25 : w * .01;
}
struct UnsupportedPart : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct Ocs {
    double sign;
    Vec2 world(Vec2 v) const { return {sign * v.x, v.y}; }
};
struct Import {
    Transaction& tx;
    IoResult& res;
    std::string page = "ANSI_1252";
    bool declaredLegacyPage = false;
    std::map<std::string, LayerId> layers{{"0", DefaultLayer}};
    std::map<LayerId, Layer> locked;
    size_t entities = 0;
    std::set<std::string> declared;
    std::map<std::string, DimStyle> styles;
    std::map<std::string, std::shared_ptr<const BlockDefinition>> blocks;
    void dimStyle(const Record& r) {
        DimStyle s;
        s.arrowSize = r.n(41, 2.5);
        s.textHeight = r.n(140, 2.5);
        s.extensionGap = r.n(42, .625);
        s.extensionOverrun = r.n(44, 1.25);
        s.textGap = std::abs(r.n(147, .625));
        s.precision = r.i(271, 2);
        if (r.n(142) > 0) {
            s.arrow = DimStyle::Arrow::Tick;
            s.arrowSize = r.n(142);
        }
        auto post = text(r, 3);
        if (auto i = post.find("<>"); i != post.npos) {
            s.prefix = post.substr(0, i);
            s.suffix = post.substr(i + 2);
        }
        styles[symbolKey(text(r, 2))] = s;
    }
    Import(Transaction& t, IoResult& result) : tx(t), res(result) {}
    std::string text(const Record& r, int c, std::string def = {}) const {
        return stringValue(r.str(c, std::move(def)));
    }
    std::string stringValue(std::string_view raw) const { return decode(raw, page, declaredLegacyPage); }
    void header(const Record& r) {
        bool legacy = false, declared = false;
        for (size_t j = 1; j < r.groups.size(); ++j) {
            auto& k = r.groups[j - 1];
            auto& v = r.groups[j];
            if (k.code != 9)
                continue;
            if (k.value == "$ACADVER")
                legacy = v.value.starts_with("AC10") && v.value < "AC1021";
            if (k.value == "$DWGCODEPAGE") {
                declared = true;
                page = upper(trim(v.value));
            }
            if (k.value == "$INSUNITS" && integer(v.value) != 0 && integer(v.value) != 4)
                res.warn(v.line, "$INSUNITS",
                         "model units retained without rescaling; export assumes millimetres");
            if (k.value == "$ACADVER" && v.value != "AC1015" && v.value != "AC1018")
                res.warn(v.line, r.type, "not AC1015; reading supported 2D subset");
        }
        declaredLegacyPage = legacy && declared;
    }
    void layer(const Record& r) {
        r.warnIfPartial(res);
        Layer l(text(r, 2));
        int index = r.i(62, 7);
        if (index == 0 || index < -255 || index > 255)
            throw std::invalid_argument("invalid layer ACI");
        l.color = color(r, std::abs(index));
        l.isVisible = index > 0;
        l.isFrozen = (r.i(70) & 1) != 0;
        l.isLocked = false;
        l.lineType = text(r, 6, "Continuous");
        l.lineWeight = weight(r, r.i(370, -3));
        auto key = symbolKey(l.name);
        if (!declared.insert(key).second) {
            res.warn(r.line, "LAYER", "duplicate layer definition; kept first");
            return;
        }
        LayerId id;
        if (auto it = layers.find(key); it != layers.end()) {
            id = it->second;
            tx.updateLayer(id, l);
        } else {
            if (layers.size() >= maxLayers)
                throw std::invalid_argument("duplicate or excessive layers");
            id = tx.addLayer(l);
            layers.emplace(key, id);
        }
        if (r.i(70) & 4) {
            l.isLocked = true;
            locked.insert_or_assign(id, std::move(l));
        }
    }
    LayerId layerId(const Record& r) {
        auto name = text(r, 8, "0");
        auto key = symbolKey(name);
        if (auto it = layers.find(key); it != layers.end())
            return it->second;
        if (layers.size() >= maxLayers)
            throw std::invalid_argument("too many layers");
        auto id = tx.addLayer(Layer(name));
        layers[key] = id;
        res.warn(r.line, "LAYER", "created undeclared entity layer");
        return id;
    }
    std::unique_ptr<Entity> label(const Record& r, Ocs o) {
        auto str = text(r, 1);
        double h = r.required(40), width = r.n(41, 1);
        if (h <= 0 || width <= 0)
            throw std::invalid_argument("invalid text height/width");
        BoundingBox2D ink{{0, 0}, {.6 * h * unicode(str).size(), h}};
        auto ext = r.field("INK");
        if (ext.size() >= 4 && ext[0].code == 1040 && ext[1].code == 1040 && ext[2].code == 1040 &&
            ext[3].code == 1040)
            ink = {{real(ext[0].value), real(ext[1].value)}, {real(ext[2].value), real(ext[3].value)}};
        double a = r.n(50) * rad, sy = h / (ink.max().y - ink.min().y), sx = sy * width;
        int hor = r.i(72), vert = r.i(73);
        bool fit = hor == 3 || hor == 5;
        auto p = o.world(r.xy(!fit && (hor || vert) ? 11 : 10));
        if (fit) {
            auto base = r.xy(11) - r.xy(10);
            sx = base.length() / (ink.max().x - ink.min().x);
            if (hor == 3)
                sy = sx / width;
            a = std::atan2(base.y, base.x);
        }
        int gen = r.i(71);
        if (gen & 2)
            sx = -sx;
        if (gen & 4)
            sy = -sy;
        auto linear = Matrix3x3::scaling(o.sign, 1) * Matrix3x3::rotation(a) *
                      Matrix3x3(sx, sy * std::tan(r.n(51) * rad), 0, 0, sy, 0);
        if (hor || vert) {
            auto lo = ink.min(), hi = ink.max();
            double ax = hor == 2 ? hi.x : hor == 1 || hor == 4 ? std::midpoint(lo.x, hi.x) : lo.x;
            double ay = vert == 3               ? hi.y
                        : vert == 2 || hor == 4 ? std::midpoint(lo.y, hi.y)
                        : vert == 1             ? lo.y
                                                : 0;
            p = p - linear.applyVector({ax, ay});
        }
        return std::make_unique<Text>(str, ink, Matrix3x3::translation(p) * linear);
    }
    std::unique_ptr<Entity> hatch(const Record& r) {
        auto name = upper(text(r, 2));
        HatchPattern pat;
        if (r.i(70))
            pat = HatchPattern::Solid;
        else if (name == "ANSI31")
            pat = HatchPattern::ANSI31;
        else if (name == "GRID" || name == "NET")
            pat = HatchPattern::Grid;
        else if (name == "PD_CONCRETE")
            pat = HatchPattern::Concrete;
        else
            throw UnsupportedPart("unsupported hatch pattern or missing boundaries");
        size_t pos = 0;
        auto seek = [&](int code) {
            while (pos < r.groups.size() && r.groups[pos].code != code)
                ++pos;
        };
        auto take = [&](int code) {
            if (pos >= r.groups.size() || r.groups[pos].code != code)
                throw std::invalid_argument("incomplete hatch boundary");
            return std::string_view(r.groups[pos++].value);
        };
        auto point = [&](int code) {
            double x = real(take(code));
            return Vec2{x, real(take(code + 10))};
        };
        auto count = [&](int code) {
            int n = integer(take(code));
            if (n <= 0 || n > 200000)
                throw std::invalid_argument("invalid hatch count");
            return n;
        };
        seek(91);
        int n = count(91);
        std::vector<Polyline> loops;
        for (int j = 0; j < n; ++j) {
            seek(92);
            int flags = integer(take(92));
            std::vector<Vertex> v;
            if (flags & 2) {
                bool bulges = integer(take(72));
                if (!integer(take(73)))
                    throw UnsupportedPart("open hatch boundary");
                int cnt = count(93);
                for (int i = 0; i < cnt; ++i) {
                    auto p = point(10);
                    double b =
                        bulges && pos < r.groups.size() && r.groups[pos].code == 42 ? real(take(42)) : 0;
                    v.push_back({p, b});
                }
            } else {
                int cnt = count(93);
                std::optional<Vec2> end;
                for (int i = 0; i < cnt; ++i) {
                    int kind = integer(take(72));
                    auto p = point(10);
                    Vec2 q;
                    double bulge = 0;
                    if (kind == 1)
                        q = point(11);
                    else if (kind == 2) {
                        double radius = real(take(40)), a = real(take(50)) * rad, b = real(take(51)) * rad;
                        if (radius <= EPSILON)
                            throw std::invalid_argument("invalid hatch edge radius");
                        bool ccw = integer(take(73));
                        double sweep = std::fmod(b - a, 2 * std::numbers::pi);
                        if (sweep <= 0)
                            sweep += 2 * std::numbers::pi;
                        auto center = p;
                        auto at = [&](double t) { return center + Vec2{std::cos(t), std::sin(t)} * radius; };
                        if (!ccw) {
                            // With group 73 = 0 both stored angles are measured clockwise.
                            a = -a;
                            b = -b;
                            sweep = -sweep;
                        }
                        p = at(a);
                        q = at(b);
                        if (std::abs(sweep) >= 2 * std::numbers::pi - 1e-12) {
                            if (cnt != 1 || radius <= EPSILON)
                                throw std::invalid_argument("invalid circular hatch loop");
                            v = {{p, std::copysign(1., sweep)},
                                 {center - (p - center), std::copysign(1., sweep)}};
                            break;
                        }
                        bulge = std::tan(sweep / 4);
                    } else
                        throw UnsupportedPart("unsupported hatch edge type");
                    if (end && (*end - p).length() > EPSILON)
                        throw UnsupportedPart("disconnected hatch edges");
                    v.push_back({p, bulge});
                    end = q;
                }
                if (end && (*end - v.front().point).length() > EPSILON)
                    throw UnsupportedPart("open hatch edges");
            }
            loops.emplace_back(std::move(v), true);
        }
        return std::make_unique<Hatch>(std::move(loops), pat, r.n(41, 1), r.n(52) * rad);
    }
    std::unique_ptr<Entity> dimension(const Record& r) {
        auto kind = r.i(70) & 15;
        std::vector<Vec2> p;
        DimKind k;
        if (kind < 2) {
            k = kind ? DimKind::Aligned : DimKind::Linear;
            p = {r.xy(13), r.xy(14), r.xy(10)};
        } else if (kind == 3 || kind == 4) {
            k = kind == 3 ? DimKind::Diameter : DimKind::Radius;
            auto a = r.xy(10), b = r.xy(15);
            p = {kind == 3 ? midpoint(a, b) : a, b, r.get(11) ? r.xy(11) : b};
        } else if (kind == 5) {
            k = DimKind::Angular;
            p = {r.xy(15), r.xy(13), r.xy(14), r.xy(10)};
        } else if (kind == 2) {
            auto a = r.xy(13), b = r.xy(14), c = r.xy(15), e = r.xy(10);
            auto hit = intersectSupports(Segment{a, b}, Segment{c, e});
            if (hit.kind != Intersection::Kind::OnePoint)
                throw UnsupportedPart("parallel angular dimension");
            k = DimKind::Angular;
            p = {hit.first, (b - hit.first).length() > EPSILON ? b : a,
                 (e - hit.first).length() > EPSILON ? e : c, r.xy(16)};
        } else
            throw UnsupportedPart("unsupported dimension type");
        auto it = styles.find(symbolKey(text(r, 3, "Standard")));
        return std::make_unique<Dimension>(k, p, it == styles.end() ? DimStyle{} : it->second, r.n(50) * rad);
    }
    std::unique_ptr<Entity> geometry(const Record& r, Ocs o) {
        if (r.type == "INSERT") {
            auto it = blocks.find(symbolKey(text(r, 2)));
            if (it == blocks.end())
                throw UnsupportedPart("missing or unsupported block definition");
            if (r.i(70, 1) != 1 || r.i(71, 1) != 1)
                throw UnsupportedPart("array insert not supported");
            auto ref = std::make_unique<BlockReference>(it->second, r.xy(10), Vec2{r.n(41, 1), r.n(42, 1)},
                                                        r.n(50) * rad);
            if (o.sign < 0)
                ref->transform(Matrix3x3::scaling(-1, 1));
            return ref;
        }
        if (r.type == "HATCH" || r.type == "DIMENSION") {
            auto e = r.type == "HATCH" ? hatch(r) : dimension(r);
            if (o.sign < 0)
                e->transform(Matrix3x3::scaling(-1, 1));
            return e;
        }
        if (r.type == "SOLID") {
            if (r.get(13) && (r.xy(13) - r.xy(12)).length() > EPSILON)
                throw UnsupportedPart("quadrilateral SOLID not supported");
            return std::make_unique<SolidFace>(o.world(r.xy(10)), o.world(r.xy(11)), o.world(r.xy(12)));
        }
        if (r.type == "LINE")
            return std::make_unique<Line>(r.xy(10), r.xy(11));
        if (r.type == "CIRCLE")
            return std::make_unique<Circle>(o.world(r.xy(10)), r.required(40));
        if (r.type == "TEXT")
            return label(r, o);
        if (r.type == "ARC") {
            auto from = r.required(50), to = r.required(51);
            auto a = std::fmod(from, 360.), b = std::fmod(to, 360.);
            double sweep = std::fmod(b - a, 360.);
            if (sweep == 0 && from != to) {
                res.warn(r.line, r.type, "full-turn ARC imported as CIRCLE");
                return std::make_unique<Circle>(o.world(r.xy(10)), r.required(40));
            }
            if (sweep < 0)
                sweep += 360.;
            return std::make_unique<Arc>(CircularArc(o.world(r.xy(10)), r.required(40),
                                                     (o.sign > 0 ? a : 180 - a) * rad, o.sign * sweep * rad));
        }
        std::vector<Vertex> vertices;
        bool y = false;
        for (auto& g : r.groups) {
            if (g.code == 10) {
                if (!vertices.empty() && !y)
                    throw std::invalid_argument("missing vertex Y");
                vertices.push_back({{o.sign * real(g.value), 0}, 0});
                y = false;
            } else if (g.code == 20 || g.code == 42) {
                if (vertices.empty())
                    throw std::invalid_argument("missing vertex X");
                if (g.code == 20) {
                    vertices.back().point.y = real(g.value);
                    y = true;
                } else
                    vertices.back().bulge = o.sign * real(g.value);
            }
        }
        if (!y || r.i(90, -1) != static_cast<int>(vertices.size()))
            throw std::invalid_argument("incorrect LWPOLYLINE vertex count");
        return std::make_unique<Polyline>(std::move(vertices), (r.i(70) & 1) != 0);
    }
    std::unique_ptr<Entity> entity(const Record& r) {
        auto skip = [&](const char* why) {
            ++res.skipped;
            res.warn(r.line, r.type.substr(0, 80), why);
        };
        if (r.type != "LINE" && r.type != "CIRCLE" && r.type != "ARC" && r.type != "TEXT" &&
            r.type != "LWPOLYLINE" && r.type != "INSERT" && r.type != "HATCH" && r.type != "DIMENSION" &&
            r.type != "SOLID") {
            skip("unsupported entity");
            return {};
        }
        double nz = r.n(230, 1);
        if (r.type != "LINE" && (std::abs(r.n(210)) > EPSILON || std::abs(r.n(220)) > EPSILON ||
                                 std::abs(std::abs(nz) - 1) > EPSILON)) {
            skip("non-planar extrusion");
            return {};
        }
        if (r.type == "LINE" && std::abs(r.n(30) - r.n(31)) > EPSILON) {
            skip("non-planar line");
            return {};
        }
        r.warnIfPartial(res);
        if (r.type == "LWPOLYLINE" && (r.n(43) != 0 || r.get(40) || r.get(41)))
            res.warn(r.line, r.type, "variable/constant polyline width ignored");
        if (r.n(39) != 0 || r.n(30) != 0 || r.n(38) != 0)
            res.warn(r.line, r.type, "projected elevation/thickness to XY");
        if (++entities > maxEntities)
            throw std::invalid_argument("too many entities");
        std::unique_ptr<Entity> e;
        try {
            e = geometry(r, {nz < 0 ? -1. : 1.});
        } catch (const UnsupportedPart& err) {
            skip(err.what());
            return {};
        }
        e->traits.layer = layerId(r);
        e->traits.isVisible = r.i(60) == 0;
        int c = r.i(62, 256);
        if (c < -256 || c > 256)
            throw std::invalid_argument("invalid entity ACI");
        if (c < 0) {
            e->traits.isVisible = false;
            c = -c;
        }
        if (r.get(420))
            e->traits.color = rgb(r.i(420));
        else if (c == 0)
            e->traits.color = ByBlock{};
        else if (c != 256)
            e->traits.color = color(r, c);
        auto lt = text(r, 6, "BYLAYER");
        if (upper(lt) == "BYBLOCK")
            e->traits.lineType = ByBlock{};
        else if (upper(lt) != "BYLAYER")
            e->traits.lineType = lt;
        int w = r.i(370, -1);
        if (w == -2)
            e->traits.lineWeight = ByBlock{};
        else if (w != -1)
            e->traits.lineWeight = weight(r, w);
        return e;
    }
};
void parse(const fs::path& path, Transaction& tx, IoResult& res) {
    Records input(path);
    Import imp(tx, res);
    std::string section, table;
    struct RawBlock {
        std::string name, owner;
        Vec2 base;
        std::vector<Record> members;
    };
    std::map<std::string, RawBlock> raw;
    RawBlock* current = nullptr;
    std::vector<Record> model, papers;
    std::map<std::string, Layout> layouts;
    std::multimap<int, std::string> tabs;
    size_t count = 0;
    bool entities = false, eof = false;
    while (auto rec = input.next()) {
        const auto& r = *rec;
        try {
            if (eof)
                throw std::invalid_argument("records after EOF");
            if (r.type == "SECTION") {
                if (!section.empty() || !r.get(2))
                    throw std::invalid_argument("invalid SECTION");
                section = upper(trim(r.str(2)));
                if (section == "HEADER")
                    imp.header(r);
                if (section == "ENTITIES") {
                    if (entities)
                        throw std::invalid_argument("duplicate ENTITIES");
                    entities = true;
                }
                continue;
            }
            if (r.type == "ENDSEC") {
                if (section.empty() || !table.empty())
                    throw std::invalid_argument("unexpected ENDSEC");
                section.clear();
                continue;
            }
            if (r.type == "EOF") {
                if (!section.empty() || !entities)
                    throw std::invalid_argument("unfinished/missing section");
                eof = true;
                continue;
            }
            if (section.empty())
                throw std::invalid_argument("record outside SECTION");
            if (section == "TABLES") {
                if (r.type == "TABLE") {
                    if (!table.empty() || !r.get(2))
                        throw std::invalid_argument("invalid TABLE");
                    table = upper(trim(r.str(2)));
                } else if (r.type == "ENDTAB") {
                    if (table.empty())
                        throw std::invalid_argument("unexpected ENDTAB");
                    table.clear();
                } else if (table == "LAYER" && r.type == "LAYER")
                    imp.layer(r);
                else if (table == "LTYPE" && r.type == "LTYPE" && r.i(73) > 0)
                    res.warn(r.line, "LTYPE", "dash pattern not imported; linetype name retained");
                else if (table == "DIMSTYLE" && r.type == "DIMSTYLE")
                    imp.dimStyle(r);
            } else if (section == "BLOCKS") {
                if (r.type == "BLOCK") {
                    auto name = imp.text(r, 2);
                    auto key = symbolKey(name);
                    current = nullptr;
                    if (!name.starts_with('*') || upper(name).starts_with("*PAPER_SPACE")) {
                        auto [it, ok] =
                            raw.try_emplace(key, RawBlock{name, r.str(330), {r.n(10), r.n(20)}, {}});
                        if (ok)
                            current = &it->second;
                        else
                            res.warn(r.line, "BLOCK", "duplicate definition ignored");
                    }
                } else if (r.type == "ENDBLK")
                    current = nullptr;
                else if (current) {
                    if (++count > maxEntities)
                        throw std::invalid_argument("too many entities");
                    current->members.push_back(std::move(*rec));
                }
            } else if (section == "ENTITIES") {
                if (++count > maxEntities)
                    throw std::invalid_argument("too many entities");
                model.push_back(std::move(*rec)); // partition by owner once LAYOUT objects are known
            } else if (section == "OBJECTS" && r.type == "LAYOUT") {
                std::string name, owner;
                for (auto& g : r.groups) {
                    if (g.code == 1)
                        name = imp.stringValue(g.value);
                    if (g.code == 330)
                        owner = g.value;
                }
                if (upper(name) == "MODEL")
                    continue;
                double w = r.n(44), h = r.n(45);
                if (w <= 0 || h <= 0)
                    continue;
                Layout l;
                l.name = name;
                l.landscape = w >= h;
                l.frame = l.stamp = false;
                bool matched = false;
                for (auto format : {Paper::A4, Paper::A3, Paper::A2, Paper::A1}) {
                    l.paper = format;
                    if ((l.size() - Vec2{w, h}).length() < .1) {
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    res.warn(r.line, "LAYOUT", "non-ISO paper size skipped");
                    continue;
                }
                l.margins = {r.n(40), r.n(41), r.n(42), r.n(43)};
                auto x = r.field("SHEET");
                if (x.size() >= 4) {
                    l.frame = integer(x[0].value);
                    l.stamp = integer(x[1].value);
                    l.designation = imp.stringValue(x[2].value);
                    l.title = imp.stringValue(x[3].value);
                }
                if (layouts.contains(owner))
                    throw std::invalid_argument("duplicate paper space owner");
                tabs.emplace(r.i(71), owner);
                layouts[owner] = std::move(l);
            }
        } catch (const std::logic_error& e) {
            throw Failure(IoStatus::InvalidData, e.what(), r.line, r.type);
        }
    }
    if (!eof)
        throw Failure(IoStatus::InvalidData, "missing EOF (truncated DXF)");
    BlockDependencies graph;
    for (auto& [key, b] : raw) {
        if (b.name.starts_with('*')) {
            for (auto& r : b.members)
                papers.push_back(std::move(r));
            continue;
        }
        auto& deps = graph[key];
        for (auto& r : b.members)
            if (r.type == "INSERT") {
                auto target = symbolKey(imp.text(r, 2));
                if (raw.contains(target) && !raw.at(target).name.starts_with('*'))
                    deps.push_back(target);
            }
    }
    auto parseEntity = [&](const Record& r) {
        try {
            return imp.entity(r);
        } catch (const std::logic_error& err) {
            throw Failure(IoStatus::InvalidData, err.what(), r.line, r.type);
        }
    };
    for (auto& key : blockOrder(graph)) {
        auto& b = raw.at(key);
        std::vector<std::unique_ptr<Entity>> owned;
        std::vector<const Entity*> members;
        for (auto& r : b.members)
            if (auto e = parseEntity(r)) {
                if (b.base != Vec2{})
                    e->transform(Matrix3x3::translation(-b.base));
                members.push_back(e.get());
                owned.push_back(std::move(e));
            }
        if (!members.empty())
            imp.blocks[key] = tx.defineBlock(b.name, members);
        else
            res.warn(0, "BLOCK", "empty block definition skipped");
    }
    for (auto& r : model) {
        if (r.i(67) || layouts.contains(r.str(330)))
            papers.push_back(std::move(r));
        else if (auto e = parseEntity(r))
            tx.add(*e);
    }
    for (auto& r : papers) {
        auto it = layouts.find(r.str(330));
        if (it == layouts.end()) {
            ++res.skipped;
            res.warn(r.line, r.type, "unsupported paper space");
            continue;
        }
        auto& l = it->second;
        if (r.type == "VIEWPORT") {
            if (r.i(69) == 1 || r.i(68) == 0)
                continue;
            if (std::abs(r.n(51)) > EPSILON || std::abs(r.n(16)) + std::abs(r.n(26)) > EPSILON) {
                res.warn(r.line, r.type, "rotated/3D viewport skipped");
                continue;
            }
            auto p = r.xy(10);
            Vec2 half{r.required(40) / 2, r.required(41) / 2};
            BoundingBox2D box(p - half, p + half);
            if (!BoundingBox2D({}, l.size()).contains(box)) {
                res.warn(r.line, r.type, "off-page viewport skipped");
                continue;
            }
            l.viewports.emplace_back(box, r.xy(12) + Vec2{r.n(17), r.n(27)}, 2 * half.y / r.required(45));
        } else if (r.field("FURNITURE").empty()) {
            if (auto e = parseEntity(r))
                l.paperEntities.emplace_back(std::move(e));
        }
    }
    for (auto& [_, owner] : tabs)
        tx.addLayout(std::move(layouts.at(owner)));
    for (auto& [id, l] : imp.locked)
        tx.updateLayer(id, l);
}
} // namespace
IoResult DxfHandler::read(const fs::path& p, Document& d) const {
    return checked([&](IoResult& r) { d.transact([&](Transaction& tx) { parse(p, tx, r); }); });
}
} // namespace pd::io
