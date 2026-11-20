// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <string_view>

namespace pd {
// Text at the extension boundary: Unicode scalar values, no NUL (also representable in SDK C strings).
inline bool validText(std::string_view s) noexcept {
    for (std::size_t i = 0; i < s.size();) {
        const auto lead = static_cast<unsigned char>(s[i++]);
        if (!lead)
            return false;
        if (lead < 0x80)
            continue;
        if (lead < 0xc2 || lead > 0xf4)
            return false;
        unsigned more = lead < 0xe0 ? 1 : lead < 0xf0 ? 2 : 3;
        if (more > s.size() - i)
            return false;
        auto second = static_cast<unsigned char>(s[i]);
        if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
            (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90))
            return false;
        while (more--)
            if ((static_cast<unsigned char>(s[i++]) & 0xc0) != 0x80)
                return false;
    }
    return true;
}
} // namespace pd
