// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pdf_font.hpp"
#include "vector_sheet.hpp"
#include <charconv>
#include <iomanip>
#include <numbers>
namespace pd::io {
namespace {
std::string n(double v) {
    if (!std::isfinite(v))
        throw std::invalid_argument("non-finite PDF coordinate");
    char b[768];
    auto r = std::to_chars(b, b + sizeof b, v == 0 ? 0. : v, std::chars_format::fixed);
    if (r.ec != std::errc{})
        throw std::overflow_error("PDF coordinate out of range");
    return {b, r.ptr};
}
std::string xy(Vec2 p) { return n(p.x) + " " + n(p.y); }
std::string ref(int id) { return std::to_string(id) + " 0 R"; }
class Pdf {
    std::vector<std::string> objects;

  public:
    int add(std::string s = {}) {
        objects.push_back(std::move(s));
        return static_cast<int>(objects.size());
    }
    void set(int id, std::string s) { objects.at(id - 1) = std::move(s); }
    int stream(const std::string& s) {
        return add("<< /Length " + std::to_string(s.size()) + " >>\nstream\n" + s + "\nendstream");
    }
    void save(const fs::path& file, int catalog) {
        auto f = output(file);
        f << "%PDF-1.5\n%\xe2\xe3\xcf\xd3\n";
        std::vector<std::streamoff> offsets;
        for (auto& obj : objects) {
            offsets.push_back(f.tellp());
            f << offsets.size() << " 0 obj\n" << obj << "\nendobj\n";
        }
        auto xref = f.tellp();
        if (xref > 9999999999LL)
            throw std::overflow_error("PDF exceeds classic xref limit");
        f << "xref\n0 " << objects.size() + 1 << "\n0000000000 65535 f \n";
        for (auto p : offsets)
            f << std::setfill('0') << std::setw(10) << p << " 00000 n \n";
        f << "trailer\n<< /Size " << objects.size() + 1 << " /Root " << ref(catalog) << " >>\nstartxref\n"
          << xref << "\n%%EOF\n";
        f.close();
    }
};
struct Font {
    struct Glyph {
        int code, object;
    };
    std::map<char32_t, Glyph> glyphs;
    std::string encode(Pdf& pdf, std::u32string_view text, IoResult& r) {
        std::string chars;
        for (auto c : text) {
            std::string proc;
            if (!glyphs.contains(c) && !pdfGlyph(c, proc)) {
                r.warn(0, "TEXT", "unsupported PDF stroke glyph shown as ?; Unicode retained in ActualText");
                c = U'?';
                pdfGlyph(c, proc);
            }
            auto it = glyphs.find(c);
            if (it == glyphs.end()) {
                int code = static_cast<int>(glyphs.size()) + 1;
                if (code > 255)
                    throw std::length_error("stroke font encoding exhausted");
                it = glyphs.emplace(c, Glyph{code, pdf.stream(proc)}).first;
            }
            chars += hex(it->second.code, 2);
        }
        return chars;
    }
    int finish(Pdf& pdf) {
        std::string chars = "/.notdef " + ref(pdf.stream("600 0 0 0 0 0 d1")), encoding, widths = "600",
                    mapping;
        int count = 0;
        std::vector<std::pair<char32_t, Glyph>> ordered(glyphs.begin(), glyphs.end());
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b) { return a.second.code < b.second.code; });
        for (auto& [cp, g] : ordered) {
            auto code = std::to_string(g.code), name = "/g" + code;
            chars += " " + name + " " + ref(g.object);
            encoding += " " + name;
            widths += " 600";
            if (count % 100 == 0)
                mapping += std::to_string(std::min<int>(100, static_cast<int>(glyphs.size()) - count)) +
                           " beginbfchar\n";
            mapping += std::string("<") + hex(g.code, 2) + "> <" + utf16hex(utf8(cp), false) + ">\n";
            if (++count % 100 == 0 || count == static_cast<int>(glyphs.size()))
                mapping += "endbfchar\n";
        }
        int cmap = pdf.stream(
            "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry "
            "(PlaneDraft) /Ordering (Unicode) /Supplement 0 >> def\n/CMapName /PDUnicode def\n/CMapType 2 "
            "def\n1 begincodespacerange\n<00> <FF>\nendcodespacerange\n" +
            mapping + "endcmap\nCMapName currentdict /CMap defineresource pop\nend end");
        return pdf.add("<< /Type /Font /Subtype /Type3 /FontBBox [-25 -25 550 1025] /FontMatrix [0.001 0 0 "
                       "0.001 0 0] /CharProcs << " +
                       chars + " >> /Encoding << /Type /Encoding /Differences [0 /.notdef" + encoding +
                       "] >> /FirstChar 0 /LastChar " + std::to_string(count) + " /Widths [" + widths +
                       "] /ToUnicode " + ref(cmap) + " /Resources << >> >>");
    }
};
struct PdfPath {
    Vec2 origin;
    double scale, pad;
    std::string& content;
    Vec2 point(Vec2 p) const { return (p - origin) * scale + Vec2{pad, pad}; }
    void move(Vec2 p) { content += xy(point(p)) + " m\n"; }
    void line(Vec2 p) { content += xy(point(p)) + " l\n"; }
    void close() { content += "h\n"; }
    void arc(const CircularArc& a) {
        int steps = static_cast<int>(std::ceil(std::abs(a.sweep()) / (std::numbers::pi / 8)));
        double step = a.sweep() / steps, k = (4. / 3) * std::tan(step / 4) * a.radius() * scale;
        auto tangent = [&](int i) {
            double angle = a.startAngle() + i * step;
            return Vec2{-std::sin(angle), std::cos(angle)} * k;
        };
        for (int i = 0; i < steps; ++i) {
            auto p = point(a.pointAt(double(i) / steps)), q = point(a.pointAt(double(i + 1) / steps));
            content += xy(p + tangent(i)) + " " + xy(q - tangent(i + 1)) + " " + xy(q) + " c\n";
        }
    }
};
IoResult exportDrawing(const fs::path& file, const Document& d, const Layout* layout) {
    return checked([&](IoResult& result) {
        Sheet sheet(d, result, layout);
        Pdf pdf;
        int root = pdf.add(), pages = pdf.add(), page = pdf.add();
        Font font;
        std::string data = "1 J 1 j\n", properties, all, off;
        constexpr double pt = 72 / 25.4;
        auto extent = sheet.bounds.max() - sheet.bounds.min();
        double pad = layout ? 0 : std::max(.5, sheet.maxWeight * pt / 2);
        if (pad > 7000 || !extent.finite())
            throw std::invalid_argument("PDF page extent out of range");
        double scale = std::min(pt, (14400 - 2 * pad) / std::max({1., extent.x, extent.y}));
        if (scale < pt)
            result.warn(0, "PDF", "geometry scaled to the 14400 pt page limit");
        PdfPath pen{sheet.bounds.min(), scale, pad, data};
        for (auto& [id, entities] : sheet.layers) {
            auto& l = *d.layer(id);
            auto oc = ref(pdf.add("<< /Type /OCG /Name <" + utf16hex(l.name) + "> >>")),
                 key = "/L" + std::to_string(id);
            properties += key + " " + oc + " ";
            all += oc + " ";
            if (!l.isVisible || l.isFrozen)
                off += oc + " ";
            data += "/OC " + key + " BDC\n";
            for (auto& item : entities) {
                auto* e = item.geometry;
                if (item.clip) {
                    auto& b = *item.clip;
                    data +=
                        "q\n" + xy(pen.point(b.min())) + " " + xy((b.max() - b.min()) * scale) + " re W n\n";
                }
                auto st = item.style;
                auto c = st.color;
                auto rgb = n(c.r / 255.) + " " + n(c.g / 255.) + " " + n(c.b / 255.);
                data += rgb + " RG\n" + rgb + " rg\n";
                if (auto t = dynamic_cast<const Text*>(e)) {
                    auto u = unicode(t->content());
                    auto b = t->localInk();
                    auto size = b.max() - b.min();
                    auto m = t->placement();
                    auto x = m.applyVector({size.x / (.6 * u.size()), 0}) * scale,
                         y = m.applyVector({0, size.y}) * scale, o = pen.point(m.applyPoint(b.min()));
                    auto chars = font.encode(pdf, u, result);
                    data += "/Span << /ActualText <" + utf16hex(t->content()) + "> >> BDC\nBT /PD 1 Tf " +
                            xy(x) + " " + xy(y) + " " + xy(o) + " Tm <" + chars + "> Tj ET\nEMC\n";
                } else {
                    data += n(st.lineWeight * pt) + " w\n";
                    path(*e, pen);
                    data += dynamic_cast<const SolidFace*>(e) ? "f\n" : "S\n";
                }
                if (item.clip)
                    data += "Q\n";
            }
            data += "EMC\n";
        }
        auto fonts = font.glyphs.empty() ? std::string{} : " /Font << /PD " + ref(font.finish(pdf)) + " >>";
        pdf.set(page, "<< /Type /Page /Parent " + ref(pages) + " /MediaBox [0 0 " +
                          xy(extent * scale + Vec2{2 * pad, 2 * pad}) + "] /Resources << /Properties << " +
                          properties + ">>" + fonts + " >> /Contents " + ref(pdf.stream(data)) + " >>");
        pdf.set(pages, "<< /Type /Pages /Count 1 /Kids [" + ref(page) + "] >>");
        pdf.set(root, "<< /Type /Catalog /Pages " + ref(pages) + " /OCProperties << /OCGs [" + all +
                          "] /D << /Order [" + all + "] /OFF [" + off + "] >> >> >>");
        pdf.save(file, root);
    });
}
} // namespace
IoResult PdfExporter::write(const fs::path& path, const Document& d) const {
    return exportDrawing(path, d, nullptr);
}
IoResult PdfExporter::writeLayout(const fs::path& path, const Document& d, const Layout& l) const {
    return exportDrawing(path, d, &l);
}
} // namespace pd::io
