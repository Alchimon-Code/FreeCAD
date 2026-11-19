// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sheet_example.hpp"
#include <clocale>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <locale>
#include <numbers>
#include <planedraft/entities/primitives.hpp>
#include <planedraft/io.hpp>
#include <random>
#include <sstream>

namespace {
using namespace pd;
using namespace pd::io;
namespace fs = std::filesystem;
struct Io : testing::Test {
    fs::path dir;
    IoRegistry registry;
    Document doc;
    void SetUp() override {
        for (int i = 0; i < 20; ++i) {
            dir = fs::temp_directory_path() / ("pd io test " + std::to_string(std::random_device{}()));
            if (fs::create_directory(dir))
                return;
        }
        FAIL() << "could not make test directory";
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    static std::string bytes(const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), {}};
    }
    static void put(const fs::path& p, std::string_view s) {
        std::ofstream f(p, std::ios::binary);
        f << s;
        ASSERT_TRUE(f.good());
    }
};
std::string why(const IoResult& r) {
    std::string s;
    for (auto& d : r.diagnostics)
        s += d.subject + ":" + std::to_string(d.line) + " " + d.message + "\n";
    return s;
}
void near(Vec2 a, Vec2 b) {
    EXPECT_NEAR(a.x, b.x, EPSILON);
    EXPECT_NEAR(a.y, b.y, EPSILON);
}
void populate(Document& doc) {
    doc.transact([](Transaction& tx) {
        Layer ink("Деталь");
        ink.color = {23, 121, 237};
        ink.lineType = "Draft";
        ink.lineWeight = .273;
        auto lid = tx.addLayer(ink);
        Layer hidden("Резерв");
        hidden.isFrozen = true;
        hidden.isVisible = false;
        hidden.color = {255, 0, 0};
        auto hid = tx.addLayer(hidden);
        Line l({-20.125, 7}, {-13, 11.25});
        l.traits.lineWeight = ByBlock{};
        l.traits.color = Color{42, 189, 99};
        l.traits.layer = lid;
        tx.add(l);
        Circle c({50, 34}, 5.625);
        c.traits.lineType = ByBlock{};
        c.traits.layer = hid;
        tx.add(c);
        Arc a(CircularArc({100, 101}, 13, -.6, -4.3));
        a.traits.layer = lid;
        a.traits.lineWeight = .117;
        tx.add(a);
        Polyline p({{{113, 123}, -.7}, {{142, 143}, 0}, {{152, 122}, 1.4}}, true);
        p.traits.color = ByBlock{};
        tx.add(p);
        Text t("  Разрез A<&> Ё 😀 ", {{-1, -2}, {24, 3.5}}, Matrix3x3(1.25, .2, 42, .3, -.95, -41));
        t.traits.layer = lid;
        tx.add(t);
        hidden.isLocked = true;
        tx.updateLayer(hid, hidden);
    });
}
TEST_F(Io, AllFiveKindsRoundTripInAC1015IncludingLockedLayerAndSpatialIndexes) {
    populate(doc);
    auto p = dir / fs::path(u8"мой чертёж.DXF");
    auto w = registry.write(p, doc);
    ASSERT_TRUE(w.ok()) << why(w);
    auto file = bytes(p);
    EXPECT_NE(file.find("AC1015"), file.npos);
    EXPECT_NE(file.find("\\U+D83D\\U+DE00"), file.npos);
    EXPECT_NE(file.find("1001\nPLANEDRAFT"), file.npos);
    Document restored;
    auto old = restored.add(Arc(CircularArc({500, 500}, 2, 0, .3)));
    const BoundingBox2D oldCenter{{499.9, 499.9}, {500.1, 500.1}};
    EXPECT_EQ(restored.queryExtraPoints(oldCenter), std::vector<EntityId>{old});
    int notices = 0;
    auto token = restored.observe([&](auto changes) {
        ++notices;
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].kind, Change::Kind::Reset);
        EXPECT_FALSE(restored.canUndo());
        EXPECT_EQ(restored.size(), 5u);
    });
    auto r = registry.read(p, restored);
    ASSERT_TRUE(r.ok()) << why(r);
    EXPECT_EQ(notices, 1);
    EXPECT_EQ(r.entities, 5u);
    EXPECT_EQ(r.layers, 3u);
    EXPECT_FALSE(restored.canRedo());
    auto ids = doc.entityIds(), out = restored.entityIds();
    auto ls = doc.layerIds(), lo = restored.layerIds();
    for (size_t i = 0; i < ls.size(); ++i) {
        auto& a = *doc.layer(ls[i]);
        auto& b = *restored.layer(lo[i]);
        EXPECT_EQ(a.name, b.name);
        EXPECT_EQ(a.color, b.color);
        EXPECT_EQ(a.lineType, b.lineType);
        EXPECT_DOUBLE_EQ(a.lineWeight, b.lineWeight);
        EXPECT_EQ(a.isFrozen, b.isFrozen);
        EXPECT_EQ(a.isVisible, b.isVisible);
        EXPECT_EQ(a.isLocked, b.isLocked);
    }
    for (size_t i = 0; i < ids.size(); ++i) {
        auto& a = *doc.entity(ids[i]);
        auto& b = *restored.entity(out[i]);
        EXPECT_EQ(typeid(a), typeid(b));
        EXPECT_EQ(a.traits.color, b.traits.color);
        EXPECT_EQ(a.traits.lineWeight, b.traits.lineWeight);
        EXPECT_EQ(a.traits.lineType, b.traits.lineType);
        EXPECT_EQ(doc.layer(a.traits.layer)->name, restored.layer(b.traits.layer)->name);
        near(a.getBounds().min(), b.getBounds().min());
        near(a.getBounds().max(), b.getBounds().max());
    }
    auto& l = dynamic_cast<const Line&>(*restored.entity(out[0]));
    near(l.start(), {-20.125, 7});
    near(l.end(), {-13, 11.25});
    auto& c = dynamic_cast<const Circle&>(*restored.entity(out[1]));
    near(c.center(), {50, 34});
    EXPECT_DOUBLE_EQ(c.radius(), 5.625);
    auto& a = dynamic_cast<const Arc&>(*restored.entity(out[2])).curve();
    near(a.center(), {100, 101});
    EXPECT_NEAR(a.startAngle(), -.6, EPSILON);
    EXPECT_NEAR(a.sweep(), -4.3, EPSILON);
    EXPECT_NEAR(a.radius(), 13, EPSILON);
    auto& p2 = dynamic_cast<const Polyline&>(*restored.entity(out[3]));
    EXPECT_TRUE(p2.closed());
    auto v = p2.vertices();
    auto original = dynamic_cast<const Polyline&>(*doc.entity(ids[3])).vertices();
    ASSERT_EQ(v.size(), original.size());
    for (size_t j = 0; j < v.size(); ++j) {
        near(v[j].point, original[j].point);
        EXPECT_DOUBLE_EQ(v[j].bulge, original[j].bulge);
    }
    auto& t = dynamic_cast<const Text&>(*restored.entity(out[4]));
    auto& t0 = dynamic_cast<const Text&>(*doc.entity(ids[4]));
    EXPECT_EQ(t.content(), t0.content());
    EXPECT_EQ(t.localInk(), t0.localInk());
    for (Vec2 pt : {Vec2{}, Vec2{1, 0}, Vec2{0, 1}})
        near(t.placement().applyPoint(pt), t0.placement().applyPoint(pt));
    EXPECT_TRUE(restored.queryExtraPoints(oldCenter).empty());
    EXPECT_TRUE(restored.query({{498, 498}, {502, 502}}).empty());
    EXPECT_EQ(restored.query(l.getBounds()), std::vector<EntityId>{out[0]});
    EXPECT_TRUE(restored.query(c.getBounds()).empty());
    EXPECT_EQ(restored.query(c.getBounds(), false), std::vector<EntityId>{out[1]});
    EXPECT_THROW(restored.remove(out[1]), std::invalid_argument);
}
const std::string section = "0\nSECTION\n2\nENTITIES\n", finish = "0\nENDSEC\n0\nEOF\n";
const std::string line = "0\nLINE\n10\n1.25\n20\n2\n11\n4\n21\n5.5\n";
TEST_F(Io, ForeignGroupsAreIgnoredAndUnknownTypesAreCountedAtFirstSourceLine) {
    auto p = dir / "other.dxf";
    put(p, "999\nignore\n0\nSECTION\n2\nEXTRA\n0\nLINE\n10\ncorrupt non-entity field\n0\nENDSEC\n" + section +
               "0\nHATCH\n93\nincorrect but irrelevant\n0\n3DSOLID\n1\nopaque\n0\nHATCH\n" + line +
               "102\n{APP\n10\n994\n102\n}\n5234\nnew field\n" + finish);
    auto r = registry.read(p, doc);
    ASSERT_TRUE(r.ok()) << why(r);
    ASSERT_EQ(r.entities, 1u);
    ASSERT_EQ(r.skipped, 3u);
    ASSERT_EQ(r.diagnostics.size(), 3u);
    EXPECT_EQ(r.diagnostics[0].line, 17u);
    EXPECT_EQ(r.diagnostics[0].subject, "HATCH");
    EXPECT_EQ(r.diagnostics[0].count, 2u);
    EXPECT_EQ(r.diagnostics[1].subject, "3DSOLID");
    EXPECT_EQ(r.diagnostics[2].subject, "LINE");
    EXPECT_EQ(r.diagnostics[2].line, 37u);
    near(doc.extents().min(), {1.25, 2});
}
TEST_F(Io, TrueColorOverridesAciAndWindows1251DecodesAlongsideRawUTF8) {
    std::string head = "0\nSECTION\n2\nHEADER\n9\n$DWGCODEPAGE\n3\nANSI_1251\n0\nENDSEC\n";
    auto p = dir / "utf.dxf";
    put(p, head + "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n0\nLAYER\n2\n" + "\xd1\xeb\xee\xe9" +
               "\n62\n-1\n70\n5\n420\n1193046\n0\nENDTAB\n0\nENDSEC\n" + section +
               "0\nTEXT\n8\nслой\n10\n2\n20\n3\n40\n4.5\n50\n90\n1\n" + "\xcf\xf0\xe8\xe2\xe5\xf2" +
               "\n62\n5\n420\n66051\n" + finish);
    auto r = registry.read(p, doc);
    ASSERT_TRUE(r.ok()) << why(r);
    auto& t = dynamic_cast<const Text&>(*doc.entity(doc.entityIds().front()));
    auto& layer = *doc.layer(t.traits.layer);
    EXPECT_EQ(layer.name, "Слой");
    EXPECT_EQ(layer.color, (Color{0x12, 0x34, 0x56}));
    EXPECT_FALSE(layer.isVisible);
    EXPECT_TRUE(layer.isFrozen && layer.isLocked);
    EXPECT_EQ(t.content(), "Привет");
    EXPECT_EQ(std::get<Color>(t.traits.color), (Color{1, 2, 3}));
    near(t.placement().applyPoint({}), {2, 3});
    near(t.placement().applyVector({1, 0}), {0, 1});
    EXPECT_NEAR(t.localInk().max().y, 4.5, EPSILON);
}
TEST_F(Io, StandardTrueColorOutputUsesTheCorrectDialect) {
    populate(doc);
    IoRegistry modern({}, DxfVersion::R2004);
    auto f = dir / "rgb.dxf";
    ASSERT_TRUE(modern.write(f, doc).ok());
    auto s = bytes(f);
    EXPECT_NE(s.find("AC1018"), s.npos);
    EXPECT_NE(s.find("\n420\n" + std::to_string(0x1779ed) + "\n"), s.npos);
    Document d;
    ASSERT_TRUE(registry.read(f, d).ok());
    EXPECT_EQ(d.layer(d.layerIds()[1])->color, (Color{23, 121, 237}));
}
TEST_F(Io, CounterclockwiseArcCrossingZeroAndOpenBulge) {
    auto p = dir / "arc.dxf";
    put(p, section +
               "0\nARC\n10\n0\n20\n0\n40\n2\n50\n350\n51\n10\n0\nLWPOLYLINE\n90\n2\n10\n0\n20\n0\n42\n0."
               "75\n10\n2\n20\n0\n" +
               finish);
    auto r = registry.read(p, doc);
    ASSERT_TRUE(r.ok()) << why(r);
    EXPECT_NEAR(dynamic_cast<const Arc&>(*doc.entity(1)).curve().sweep(), std::numbers::pi / 9, EPSILON);
    EXPECT_FALSE(dynamic_cast<const Polyline&>(*doc.entity(2)).closed());
    EXPECT_EQ(doc.queryExtraPoints({{-.01, -.01}, {.01, .01}}), std::vector<EntityId>{1});
    put(p, section + "0\nARC\n10\n5\n20\n6\n40\n2\n50\n0\n51\n360\n" + finish);
    r = registry.read(p, doc);
    ASSERT_TRUE(r.ok()) << why(r);
    EXPECT_NE(why(r).find("full-turn"), std::string::npos);
    EXPECT_NE(dynamic_cast<const Circle*>(doc.entity(1)), nullptr);
}
TEST_F(Io, FailureAfterValidEntityKeepsBothTreesRevisionHistoryAndObservers) {
    auto old = doc.add(Arc(CircularArc({99, 99}, 4, 0, .2)));
    doc.add(Line({3, 5}, {4, 6}));
    doc.undo();
    auto rev = doc.revision();
    int notices = 0;
    auto token = doc.observe([&](auto) { ++notices; });
    auto p = dir / "bad.dxf";
    for (auto suffix : {"", "0\nCIRCLE\n40\nnan\n0\nENDSEC\n0\nEOF\n",
                        "0\nLWPOLYLINE\n90\n3\n10\n3\n20\n2\n0\nENDSEC\n0\nEOF\n", "0\nCIRCLE\n10"}) {
        put(p, section + line + suffix);
        auto r = registry.read(p, doc);
        ASSERT_FALSE(r.ok());
        EXPECT_EQ(doc.revision(), rev);
        EXPECT_EQ(notices, 0);
        EXPECT_TRUE(doc.canRedo());
        EXPECT_EQ(doc.queryExtraPoints({{98, 98}, {100, 100}}), std::vector<EntityId>{old});
        EXPECT_TRUE(doc.query({{0, 0}, {5, 6}}).empty());
    }
    EXPECT_TRUE(doc.redo());
}
TEST_F(Io, UnsupportedSaveCannotTruncateExistingFile) {
    // Blocks are now supported. Use a deliberately unknown subtype to exercise every writer's
    // atomic refusal rather than depending on a particular CAD feature being unimplemented.
    struct Unknown final : Entity {
        BoundingBox2D getBounds() const override { return {{0, 0}, {2, 1}}; }
        void transform(const Matrix3x3&) override {}
        std::unique_ptr<Entity> clone() const override { return std::make_unique<Unknown>(*this); }
        std::vector<Vec2> getGripPoints() const override { return {}; }
        std::vector<Vec2> getSnapPoints(SnapType) const override { return {}; }
    };
    doc.add(Unknown{});
    for (auto ext : {".dxf", ".pdf", ".svg", ".unknown"}) {
        auto p = dir / (std::string("old") + ext);
        put(p, "existing");
        auto r = registry.write(p, doc);
        EXPECT_FALSE(r.ok());
        EXPECT_EQ(bytes(p), "existing");
    }
    EXPECT_EQ(std::distance(fs::directory_iterator(dir), {}), 4);
}
TEST_F(Io, PublishingRoundTripPreservesNativeTypesStylesAndNamedPaperSpace) {
    sheet_test::drawing(doc);
    auto path = dir / "plate.dxf";
    auto result = registry.write(path, doc);
    ASSERT_TRUE(result.ok()) << why(result);
    Document out;
    result = registry.read(path, out);
    ASSERT_TRUE(result.ok()) << why(result);
    EXPECT_EQ(result.skipped, 0u);
    ASSERT_EQ(out.size(), doc.size());
    ASSERT_EQ(out.blockIds().size(), 2u);
    auto pages = out.layoutIds();
    ASSERT_EQ(pages.size(), 2u);
    auto& page = *out.layout(pages[0]);
    EXPECT_EQ(page.name, "Plate");
    EXPECT_EQ(page.title, "Корпус");
    EXPECT_TRUE(page.stamp);
    near(page.size(), {297, 210});
    ASSERT_EQ(page.viewports.size(), 1u);
    EXPECT_DOUBLE_EQ(page.viewports[0].scale(), .5);
    near(page.viewports[0].camera(), sheet_test::centre);
    EXPECT_EQ(page.viewports[0].getBounds(), BoundingBox2D({65, 70}, {165, 120}));
    EXPECT_EQ(out.layout(pages[1])->name, "Деталь");
    auto src = doc.entityIds(), dst = out.entityIds();
    for (size_t i = 0; i < src.size(); ++i) {
        auto& a = *doc.entity(src[i]);
        auto& b = *out.entity(dst[i]);
        EXPECT_EQ(typeid(a), typeid(b));
        if (auto dim = dynamic_cast<const Dimension*>(&a)) {
            auto& d = dynamic_cast<const Dimension&>(b);
            EXPECT_EQ(d.kind(), dim->kind());
            EXPECT_EQ(d.defPoints(), dim->defPoints());
            EXPECT_EQ(d.style(), dim->style());
            EXPECT_EQ(d.label(), dim->label());
        }
        if (auto h = dynamic_cast<const Hatch*>(&a)) {
            auto& hh = dynamic_cast<const Hatch&>(b);
            EXPECT_EQ(hh.pattern(), h->pattern());
            ASSERT_EQ(hh.loops().size(), 2u);
            EXPECT_EQ(hh.loops()[0].vertices()[1].bulge, .4);
            EXPECT_EQ(hh.primitives().size(), h->primitives().size());
        }
        if (auto ins = dynamic_cast<const BlockReference*>(&a)) {
            auto& r = dynamic_cast<const BlockReference&>(b);
            EXPECT_EQ(r.definition().name(), "Assembly");
            near(r.placement().applyPoint({3, 7}), ins->placement().applyPoint({3, 7}));
        }
    }
    auto pdf = dir / "page.pdf", svg = dir / "page.svg";
    ASSERT_TRUE(registry.write(pdf, out, &page).ok());
    ASSERT_TRUE(registry.write(svg, out, &page).ok());
    EXPECT_NE(bytes(pdf).find(" re W n"), std::string::npos);
    EXPECT_NE(bytes(svg).find("width=\"297mm\" height=\"210mm\""), std::string::npos);
    EXPECT_NE(bytes(svg).find("<clipPath"), std::string::npos);
}
TEST_F(Io, CircularForwardInsertDoesNotReplaceExistingDrawing) {
    auto p = dir / "cycle.dxf";
    std::string blocks = "0\nSECTION\n2\nBLOCKS\n";
    auto def = [&](std::string a, std::string b) {
        blocks += "0\nBLOCK\n2\n" + a + "\n0\nINSERT\n2\n" + b + "\n10\n0\n20\n0\n0\nENDBLK\n";
    };
    def("A", "B");
    def("B", "A");
    put(p, blocks + "0\nENDSEC\n" + section + line + finish);
    doc.add(Line({4, 2}, {8, 2}));
    auto rev = doc.revision();
    auto r = registry.read(p, doc);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(why(r).find("circular"), std::string::npos);
    EXPECT_EQ(doc.revision(), rev);
    EXPECT_TRUE(doc.canUndo());
}
struct Comma : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
};
TEST_F(Io, CommaDecimalAndDigitGroupingDoNotChangeAnyFormat) {
    struct Restore {
        std::locale old;
        std::string c = std::setlocale(LC_NUMERIC, nullptr);
        ~Restore() {
            std::locale::global(old);
            std::setlocale(LC_NUMERIC, c.c_str());
        }
    } restore;
    if (const char* requested = std::getenv("PD_TEST_NUMERIC_LOCALE")) {
        ASSERT_NE(std::setlocale(LC_NUMERIC, requested), nullptr);
        ASSERT_STREQ(std::localeconv()->decimal_point, ",");
    }
    std::locale::global(std::locale(std::locale::classic(), new Comma));
    std::ostringstream check;
    check << 12.5;
    ASSERT_EQ(check.str(), "12,5");
    doc.add(Line({12345.125, 2}, {12346.25, 5}));
    auto p = dir / "locale.dxf";
    ASSERT_TRUE(registry.write(p, doc).ok());
    EXPECT_NE(bytes(p).find("12345.125"), std::string::npos);
    Document d;
    ASSERT_TRUE(registry.read(p, d).ok());
    near(d.extents().min(), {12345.125, 2});
    for (auto ext : {"svg", "pdf"})
        ASSERT_TRUE(registry.write(dir / (std::string("locale.") + ext), d).ok());
}
TEST_F(Io, VectorExportsContainLayerBoundariesStylesCoordinatesCurvesAndActualUnicode) {
    populate(doc);
    auto p = dir / "sheet.svg";
    ASSERT_TRUE(registry.write(p, doc).ok());
    auto svg = bytes(p);
    for (auto s : {"<svg xmlns=", "viewBox=", "scale(1,-1)", "<g id=\"Деталь\"", "M -20.125 7 L -13 11.25",
                   "stroke=\"#2ABD63\"", "stroke-width=\"0.273\"", " A 13 13 0 1 0 ",
                   "Резерв\" display=\"none\"", "Разрез A&lt;&amp;&gt; Ё", "</svg>"})
        EXPECT_NE(svg.find(s), svg.npos) << s;
    p = dir / "sheet.pdf";
    auto r = registry.write(p, doc);
    ASSERT_TRUE(r.ok()) << why(r);
    auto pdf = bytes(p);
    ASSERT_TRUE(pdf.starts_with("%PDF-1.5"));
    EXPECT_TRUE(pdf.ends_with("%%EOF\n"));
    for (auto s :
         {"/MediaBox [0 0 ", "/OCProperties", "/OFF [", "/Type /OCG /Name <FEFF0414043504420430043B044C>",
          "/OC /L0 BDC", " c\n", "/Subtype /Type3", "/ToUnicode", "/ActualText <FEFF"})
        EXPECT_NE(pdf.find(s), pdf.npos) << s;
    EXPECT_EQ(pdf.find("/Subtype /Image"), pdf.npos);
    auto x = std::stoull(pdf.substr(pdf.rfind("startxref\n") + 10));
    ASSERT_EQ(pdf.substr(x, 4), "xref");
    std::istringstream refs(pdf.substr(x));
    std::string word;
    int first, count, generation;
    size_t offset;
    refs >> word >> first >> count;
    ASSERT_EQ(first, 0);
    for (int i = 0; i < count; ++i) {
        ASSERT_TRUE(refs >> offset >> generation >> word);
        if (i) {
            EXPECT_TRUE(pdf.substr(offset).starts_with(std::to_string(i) + " 0 obj\n"));
        }
    }
}
TEST_F(Io, TrueScalePdfAndDegenerateDrawingViewBox) {
    doc.add(Line({1e7, 1e7}, {1e7 + 25.4, 1e7}));
    auto file = dir / "page.pdf";
    ASSERT_TRUE(registry.write(file, doc).ok());
    auto pdf = bytes(file);
    auto start = pdf.find("/MediaBox [0 0 ");
    ASSERT_NE(start, pdf.npos);
    double width, height;
    std::istringstream v(pdf.substr(start + 15));
    v >> width >> height;
    EXPECT_NEAR(width, 73, 1e-7);
    EXPECT_DOUBLE_EQ(height, 1);
    EXPECT_NE(pdf.find("0.5 0.5 m\n"), pdf.npos);
    Document empty;
    ASSERT_TRUE(registry.write(dir / "empty.svg", empty).ok());
    empty.add(Line({1, 2}, {11, 22}));
    ASSERT_TRUE(registry.write(dir / "metric.svg", empty).ok());
    EXPECT_NE(bytes(dir / "metric.svg").find("viewBox=\"0.875 -22.125 10.25 20.25\""), std::string::npos);
}
TEST_F(Io, AbsentDwgToolsAreDiagnosedWithoutChangingAnything) {
    IoRegistry none(DwgTools{});
    auto p = dir / "missing.dwg";
    doc.add(Line({1, 2}, {2, 3}));
    EXPECT_EQ(none.read(p, doc).status, IoStatus::BackendUnavailable);
    put(p, "keep");
    EXPECT_EQ(none.write(p, doc).status, IoStatus::BackendUnavailable);
    EXPECT_EQ(bytes(p), "keep");
    EXPECT_EQ(doc.size(), 1u);
    EXPECT_TRUE(doc.canUndo());
}
#ifdef PD_TEST_WORKER
TEST_F(Io, BridgePassesLiteralArgumentsAndReportsWorkerFailureWithoutPublication) {
    IoRegistry bridge({PD_TEST_WORKER, PD_TEST_WORKER});
    auto p = dir / fs::path(u8"ю ; $ (test).dwg");
    doc.add(Line({1, 2}, {4, 5}));
    auto w = bridge.write(p, doc);
    ASSERT_TRUE(w.ok()) << why(w);
    EXPECT_NE(why(w).find("fixture stderr"), std::string::npos);
    Document d;
    ASSERT_TRUE(bridge.read(p, d).ok());
    EXPECT_EQ(d.size(), 1u);
    put(p, "fail");
    auto r = bridge.read(p, d);
    EXPECT_EQ(r.status, IoStatus::BackendError);
    EXPECT_NE(why(r).find("exit code 23"), std::string::npos);
    EXPECT_EQ(d.size(), 1u);
}
#endif
#ifdef PD_TEST_REAL_DWG
TEST_F(Io, SeparatelyInstalledDwgToolsRoundTripOurOwnDrawing) {
    auto p = dir / "real drawing.dwg";
    doc.add(Line({10.5, 3}, {15, 4}));
    auto w = registry.write(p, doc);
    ASSERT_TRUE(w.ok()) << why(w);
    Document d;
    auto r = registry.read(p, d);
    ASSERT_TRUE(r.ok()) << why(r);
    ASSERT_EQ(d.size(), 1u);
    near(d.extents().min(), {10.5, 3});
    near(d.extents().max(), {15, 4});
}
#endif
} // namespace
