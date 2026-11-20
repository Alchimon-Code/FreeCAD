// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <cstdint>
namespace pd {
using EntityId = std::uint64_t;
using LayerId = std::uint64_t;
using BlockId = std::uint64_t;
inline constexpr LayerId DefaultLayer = 0;
} // namespace pd
