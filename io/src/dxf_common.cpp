// SPDX-License-Identifier: LGPL-2.1-or-later
#include "dxf_common.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
namespace pd::io::dxf {
std::string_view trim(std::string_view s) {
    auto a = s.find_first_not_of(" \t\r");
    if (a == s.npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}
std::string upper(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'a' && c <= 'z')
            c -= 32;
    return r;
}
// CAD symbol lookup is case-insensitive. Do not use the user's C locale to fold UTF-8 bytes.
std::string symbolKey(std::string_view name) {
    std::string key;
    for (auto c : unicode(name)) {
        if ((c >= U'a' && c <= U'z') || (c >= U'а' && c <= U'я'))
            c -= 32;
        else if (c == U'ё')
            c = U'Ё';
        key += utf8(c);
    }
    return key;
}
template <class T> T number(std::string_view s) {
    s = trim(s);
    if (s.starts_with('+'))
        s.remove_prefix(1);
    T v{};
    if (s.empty())
        throw std::invalid_argument("missing numeric value");
    auto end = s.data() + s.size();
    auto [p, ec] = std::from_chars(s.data(), end, v);
    if (ec != std::errc{} || p != end || !std::isfinite(v))
        throw std::invalid_argument("invalid DXF number");
    return v;
}
int integer(std::string_view s) { return number<int>(s); }
double real(std::string_view s) { return number<double>(s); }
Color aci(int i) {
    static constexpr Color basic[] = {{0, 0, 0},       {255, 0, 0},    {255, 255, 0}, {0, 255, 0},
                                      {0, 255, 255},   {0, 0, 255},    {255, 0, 255}, {255, 255, 255},
                                      {128, 128, 128}, {192, 192, 192}};
    if (i < 1 || i > 255)
        throw std::invalid_argument("invalid ACI color");
    if (i < 10)
        return basic[i];
    if (i >= 250) {
        constexpr uint8_t gray[] = {51, 80, 105, 130, 190, 255};
        auto v = gray[i - 250];
        return {v, v, v};
    }
    const int row = (i - 10) / 10, shade = i % 10;
    constexpr int levels[] = {255, 165, 127, 76, 38};
    int hi = levels[shade / 2], lo = shade % 2 ? hi / 2 : 0;
    int up = lo + (hi - lo) * (row % 4) / 4, down = lo + (hi - lo) * (4 - row % 4) / 4;
    auto rgb = [](int r, int g, int b) { return Color{(uint8_t)r, (uint8_t)g, (uint8_t)b}; };
    switch (row / 4) {
    case 0:
        return rgb(hi, up, lo);
    case 1:
        return rgb(down, hi, lo);
    case 2:
        return rgb(lo, hi, up);
    case 3:
        return rgb(lo, down, hi);
    case 4:
        return rgb(up, lo, hi);
    default:
        return rgb(hi, lo, down);
    }
}
int closestAci(Color c) {
    int best = 1, dist = 200000;
    for (int i = 1; i <= 255; i++) {
        auto p = aci(i);
        int r = c.r - p.r, g = c.g - p.g, b = c.b - p.b, d = r * r + g * g + b * b;
        if (d < dist) {
            best = i;
            dist = d;
            if (!d)
                break;
        }
    }
    return best;
}
int weightCode(double mm) {
    constexpr int allowed[] = {0,  5,  9,  13, 15, 18,  20,  25,  30,  35,  40,  50,
                               53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};
    return *std::min_element(std::begin(allowed), std::end(allowed),
                             [&](int a, int b) { return std::abs(mm - a * .01) < std::abs(mm - b * .01); });
}
// For legacy DXF, names in symbol tables, INSERTs and the layout dictionary must agree
// byte-for-byte. Real codepage text also works with CAD tools that only expand \U+ in TEXT.
static constexpr char32_t c1251[] =
    U"ЂЃ‚ѓ„…†‡€‰Љ‹ЊЌЋЏђ‘’“”•–—\u0098™љ›њќћџ ЎўЈ¤Ґ¦§Ё©Є«¬\u00ad®Ї°±Ііґµ¶·ё№є»јЅѕї";
static constexpr char32_t c1252[] = U"€\u0081‚ƒ„…†‡ˆ‰Š‹Œ\u008dŽ\u008f\u0090‘’“”•–—˜™š›œ\u009džŸ";
static_assert(std::size(c1251) == 65 && std::size(c1252) == 33);
std::string encode(std::string_view s) {
    std::string r;
    for (auto c : unicode(s)) {
        if (c < 32 || c == 127)
            throw std::invalid_argument("control character not representable in single-line DXF string");
        if (c < 127 && c != '\\')
            r += static_cast<char>(c);
        else if (c >= U'А' && c <= U'я')
            r += static_cast<char>(0xc0 + c - U'А');
        else if (auto at = std::u32string_view(c1251).find(c); at != std::u32string_view::npos && c != 0x98)
            r += static_cast<char>(0x80 + at);
        else if (c < 65536)
            r += "\\U+" + hex(c, 4);
        else {
            c -= 65536;
            r += "\\U+" + hex(0xd800 + (c >> 10), 4) + "\\U+" + hex(0xdc00 + (c & 1023), 4);
        }
    }
    return r;
}
static char32_t codeUnit(std::string_view s) {
    unsigned v = 0;
    if (s.size() < 7 || !s.starts_with("\\U+"))
        return 0;
    auto end = s.data() + 7;
    auto [p, e] = std::from_chars(s.data() + 3, end, v, 16);
    return e == std::errc{} && p == end ? v : 0;
}
std::string decode(std::string_view raw, std::string_view page, bool declaredLegacyPage) {
    std::string s(raw);
    bool native = declaredLegacyPage;
    if (!native) {
        try {
            (void)unicode(raw);
        } catch (const std::invalid_argument&) {
            native = true;
        }
    }
    if (native) {
        s.clear();
        for (unsigned char c : raw) {
            if (!c)
                throw std::invalid_argument("NUL in DXF string");
            char32_t v = c;
            if (c >= 128) {
                if (page == "ANSI_1251")
                    v = c >= 192 ? 0x410 + c - 192 : c1251[c - 128];
                else if (page == "ANSI_1252" || page.empty())
                    v = c < 160 ? c1252[c - 128] : c;
                else
                    throw std::invalid_argument("unsupported DXF text codepage");
            }
            s += utf8(v);
        }
    }
    std::string out;
    for (size_t i = 0; i < s.size();) {
        auto c = codeUnit(std::string_view(s).substr(i));
        if (!c) {
            out += s[i++];
            continue;
        }
        i += 7;
        if (c >= 0xd800 && c < 0xdc00) {
            auto l = codeUnit(std::string_view(s).substr(i));
            if (l < 0xdc00 || l >= 0xe000)
                throw std::invalid_argument("unpaired DXF Unicode surrogate");
            i += 7;
            c = 0x10000 + ((c - 0xd800) << 10) + (l - 0xdc00);
        } else if (c >= 0xdc00 && c < 0xe000)
            throw std::invalid_argument("unpaired DXF Unicode surrogate");
        out += utf8(c);
    }
    return out;
}
} // namespace pd::io::dxf
