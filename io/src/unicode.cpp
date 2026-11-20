// SPDX-License-Identifier: LGPL-2.1-or-later
#include "detail.hpp"
namespace pd::io {
std::u32string unicode(std::string_view s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        unsigned c = (unsigned char)s[i++], n = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            c &= 31;
            n = 1;
            minimum = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            c &= 15;
            n = 2;
            minimum = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            c &= 7;
            n = 3;
            minimum = 65536;
        } else if (c >= 128 || c == 0)
            throw std::invalid_argument("invalid UTF-8");
        while (n--) {
            if (i == s.size() || ((unsigned char)s[i] & 0xc0) != 128)
                throw std::invalid_argument("invalid UTF-8 continuation");
            c = (c << 6) | ((unsigned char)s[i++] & 63);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c < 0xe000))
            throw std::invalid_argument("invalid Unicode scalar");
        out += static_cast<char32_t>(c);
    }
    return out;
}
std::string utf8(char32_t c) {
    if (c <= 127)
        return {static_cast<char>(c)};
    std::string s;
    unsigned n = c <= 2047 ? 1 : c <= 65535 ? 2 : 3;
    s += static_cast<char>((n == 1 ? 0xc0 : n == 2 ? 0xe0 : 0xf0) | (c >> (6 * n)));
    while (n--)
        s += static_cast<char>(128 | ((c >> (6 * n)) & 63));
    return s;
}
std::string utf16hex(std::string_view s, bool bom) {
    std::string r = bom ? "FEFF" : "";
    for (auto c : unicode(s)) {
        if (c <= 65535)
            r += hex(c, 4);
        else {
            c -= 65536;
            r += hex(0xd800 + (c >> 10), 4) + hex(0xdc00 + (c & 1023), 4);
        }
    }
    return r;
}
} // namespace pd::io
