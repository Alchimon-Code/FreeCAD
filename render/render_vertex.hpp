// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <cstdint>
#include <limits>

namespace pd {
struct Ink {
    std::uint8_t r, g, b, a = 255;
};
struct RenderVertex {
    float x, y;
    Ink ink;
};
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24);
static_assert(sizeof(RenderVertex) == 12);
inline constexpr Ink selectionInk{255, 191, 61}, gripInk{53, 144, 255}, hotGripInk{255, 68, 68};

} // namespace pd
