// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "detail.hpp"
#include <numbers>
namespace pd::io::dxf {
// Writers must not publish a file that exceeds our reader's exchange limits.
inline constexpr size_t maxLineBytes = 1024 * 1024, maxRecordGroups = 1000000, maxEntities = 1000000,
                        maxLayers = 4096;
inline constexpr double rad = std::numbers::pi / 180;
std::string_view trim(std::string_view);
std::string upper(std::string_view);
std::string symbolKey(std::string_view utf8Name);
int integer(std::string_view);
double real(std::string_view);
Color aci(int index);
int closestAci(Color);
int weightCode(double mm);
std::string encode(std::string_view utf8);
std::string encodeAscii(std::string_view utf8);
std::string decode(std::string_view bytes, std::string_view codepage, bool declaredLegacyPage);
} // namespace pd::io::dxf
