// SPDX-License-Identifier: LGPL-2.1-or-later
#include "vector_sheet.hpp"
#include <set>
namespace pd::io {
namespace {
std::string xml(std::string_view s) {
    std::string r;
    for (auto c : unicode(s)) {
        switch (c) {
        case '&':
            r += "&amp;";
            break;
        case '<':
            r += "&lt;";
            break;
        case '>':
            r += "&gt;";
            break;
        case '"':
            r += "&quot;";
            break;
        default:
            if ((c < 32 && c != '\t' && c != '\n' && c != '\r') || c == 0xfffe || c == 0xffff)
                throw std::invalid_argument("invalid XML character");
            r += utf8(c);
        }
    }
    return r;
}
std::string idFor(const Layer& l, std::set<std::string>& used) {
    std::string id;
    for (auto c : unicode(l.name)) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || (c >= 0x400 && c <= 0x4ff))
            id += utf8(c);
        else
            id += '_';
    }
    if (id.empty() || (id[0] >= '0' && id[0] <= '9') || id[0] == '-' || id[0] == '.')
        id = "layer_" + id;
    while (!used.insert(id).second) {
        id.push_back('_');
        id += std::to_string(l.id());
    }
    return id;
}
std::string xy(Vec2 p) { return num(p.x) + " " + num(p.y); }
struct SvgPath {
    std::ostream& f;
    void move(Vec2 p) { f << "M " << xy(p); }
    void line(Vec2 p) { f << " L " << xy(p); }
    void close() { f << " Z"; }
    void arc(const CircularArc& a) {
        auto r = num(a.radius());
        f << " A " << r << ' ' << r << " 0 " << (std::abs(a.sweep()) > std::numbers::pi) << ' '
          << (a.sweep() > 0) << ' ' << xy(a.pointAt(1));
    }
};
IoResult exportDrawing(const fs::path& file, const Document& d, const Layout* layout) {
    return checked([&](IoResult& result) {
        Sheet sheet(d, result, layout);
        auto f = output(file);
        auto lo = sheet.bounds.min(), hi = sheet.bounds.max();
        double pad = layout ? 0 : std::max(.005, sheet.maxWeight / 2);
        auto span = hi - lo + Vec2{2 * pad, 2 * pad};
        f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
          << num(span.x) << "mm\" height=\"" << num(span.y) << "mm\" viewBox=\""
          << xy({lo.x - pad, -hi.y - pad}) << ' ' << xy(span)
          << "\">\n<g transform=\"scale(1,-1)\" stroke-linejoin=\"round\" stroke-linecap=\"round\">\n";
        std::set<std::string> ids;
        std::map<std::string, std::string> clips;
        if (layout) {
            f << "<defs>\n";
            for (auto& [_, items] : sheet.layers)
                for (auto& item : items) {
                    auto& b = *item.clip;
                    auto key = xy(b.min()) + " " + xy(b.max());
                    if (clips.contains(key))
                        continue;
                    auto id = "pdClip" + std::to_string(clips.size());
                    clips[key] = id;
                    ids.insert(id);
                    auto size = b.max() - b.min();
                    f << "<clipPath id=\"" << id << "\"><rect x=\"" << num(b.min().x) << "\" y=\""
                      << num(b.min().y) << "\" width=\"" << num(size.x) << "\" height=\"" << num(size.y)
                      << "\"/></clipPath>\n";
                }
            f << "</defs>\n";
        }
        for (auto& [id, entities] : sheet.layers) {
            auto& l = *d.layer(id);
            f << "<g id=\"" << idFor(l, ids) << "\""
              << (l.isFrozen || !l.isVisible ? " display=\"none\"" : "") << "><title>" << xml(l.name)
              << "</title>\n";
            for (auto& item : entities) {
                auto* e = item.geometry;
                if (item.clip)
                    f << "<g clip-path=\"url(#" << clips.at(xy(item.clip->min()) + " " + xy(item.clip->max()))
                      << ")\">";
                auto style = item.style;
                auto c = style.color;
                std::string color = "#";
                color += hex(c.r, 2);
                color += hex(c.g, 2);
                color += hex(c.b, 2);
                if (auto t = dynamic_cast<const Text*>(e)) {
                    auto m = t->placement();
                    auto x = m.applyVector({1, 0}), y = m.applyVector({0, -1}), o = m.applyPoint({});
                    auto ink = t->localInk();
                    auto extent = ink.max() - ink.min();
                    f << "<text xml:space=\"preserve\" fill=\"" << color << "\" transform=\"matrix(" << xy(x)
                      << ' ' << xy(y) << ' ' << xy(o) << ")\" x=\"" << num(ink.min().x) << "\" y=\""
                      << num(-ink.min().y) << "\" font-size=\"" << num(extent.y) << "\" textLength=\""
                      << num(extent.x) << "\" lengthAdjust=\"spacingAndGlyphs\">" << xml(t->content())
                      << "</text>\n";
                } else {
                    bool fill = dynamic_cast<const SolidFace*>(e);
                    f << "<path fill=\"" << (fill ? color : "none") << "\" stroke=\""
                      << (fill ? "none" : color) << "\" stroke-width=\""
                      << num(style.lineWeight > 0 ? style.lineWeight : .01) << "\" d=\"";
                    SvgPath sink{f};
                    path(*e, sink);
                    f << "\"/>\n";
                }
                if (item.clip)
                    f << "</g>\n";
            }
            f << "</g>\n";
        }
        f << "</g></svg>\n";
        f.close();
    });
}
} // namespace
IoResult SvgExporter::write(const fs::path& path, const Document& d) const {
    return exportDrawing(path, d, nullptr);
}
IoResult SvgExporter::writeLayout(const fs::path& path, const Document& d, const Layout& l) const {
    return exportDrawing(path, d, &l);
}
} // namespace pd::io
