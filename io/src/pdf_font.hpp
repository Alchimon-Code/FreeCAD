// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <string>
namespace pd::io {
// Original compact drafting strokes, not an imported TTF/SHX. Empty ink is valid for a space.
bool pdfGlyph(char32_t scalar, std::string& charProc);
} // namespace pd::io
