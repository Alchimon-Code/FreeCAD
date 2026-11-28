// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/math/edges.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pd {
// AutoCAD keeps a linetype as lengths along the curve in drawing units: a positive value is ink, a
// negative one a gap, and a zero a dot. A drawing carries its own LTYPE table, and the standard names
// below are what real drawings fall back on when it does not; the lengths are the long ISO ones, so a
// hidden edge reads as hidden at the zoom a 2D sheet is drawn at.
inline const std::map<std::string, std::vector<double>>& standardDashPatterns() {
    static const std::map<std::string, std::vector<double>> table{
        {"DASHED", {12, -3}},
        {"DASHED2", {12, -3}},
        {"DASHEDX2", {24, -6}},
        {"HIDDEN", {6, -1.5}},
        {"HIDDEN2", {6, -1.5}},
        {"HIDDENX2", {12, -3}},
        {"CENTER", {24, -3, 3, -3}},
        {"CENTER2", {24, -3, 3, -3}},
        {"CENTERX2", {48, -6, 6, -6}},
        {"PHANTOM", {24, -6, 6, -6, 6, -6}},
        {"DASHDOT", {12, -3, 0, -3}},
        {"DIVIDE", {12, -3, 0, -3}},
        {"DOT", {0, -3}},
        {"BORDER", {6, -1.5}},
        {"ISO02W100", {12, -3}},
        {"ISO03W100", {12, -6}},
        {"ISO04W100", {24, -3, 3, -3}},
        {"ISO05W100", {24, -6, 6, -6, 6, -6}},
        {"ISO07W100", {0, -3}},
        // Russian drawings name the same lines by their shape rather than by their pattern.
        {"ШТРИХОВАЯ", {6, -1.5}},
        {"ОСЕВАЯ", {24, -3, 3, -3}},
        {"ШТРИХПУНКТИРНАЯ", {24, -3, 3, -3}},
        {"ПУНКТИРНАЯ", {0, -3}},
    };
    return table;
}

// Splits a stroke into the pieces that carry ink. The phase carries over from one segment to the next,
// which is what makes a dashed circle read as one dashed circle instead of a row of dashed chords.
class Dasher {
  public:
    explicit Dasher(std::string_view lineType, double scale = 1) {
        std::string name;
        for (char c : lineType)
            name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        auto pattern = standardDashPatterns().find(name);
        if (pattern == standardDashPatterns().end() || !(scale > 0))
            return;
        for (double value : pattern->second) {
            // A zero in the table is a dot: it stays ink, but only as a speck on the line.
            const double length = value == 0 ? 0.2 * scale : std::abs(value) * scale;
            tokens_.push_back({length, value >= 0});
        }
        left_ = tokens_.front().first;
    }
    // A pattern as the drawing carried it: positive lengths are ink, negative ones gaps, a zero a dot.
    explicit Dasher(std::span<const double> pattern, double scale = 1) {
        if (!(scale > 0))
            return;
        for (double value : pattern)
            tokens_.push_back({(value == 0 ? 0.2 : std::abs(value)) * scale, value >= 0});
        if (!tokens_.empty())
            left_ = tokens_.front().first;
    }
    // Continuous strokes, and every name this table does not know, are drawn whole.
    [[nodiscard]] bool solid() const { return tokens_.empty(); }

    template <class Emit> void split(Segment segment, Emit&& emit) {
        const double length = (segment.b - segment.a).length();
        if (length <= 0)
            return;
        if (tokens_.empty()) {
            emit(segment);
            return;
        }
        const Vec2 step = (segment.b - segment.a) * (1 / length);
        double at = 0;
        while (at < length) {
            const double piece = std::min(left_, length - at);
            if (tokens_[current_].second)
                emit(Segment{segment.a + step * at, segment.a + step * (at + piece)});
            at += piece;
            left_ -= piece;
            if (left_ <= 1e-9) {
                current_ = (current_ + 1) % tokens_.size();
                left_ = tokens_[current_].first;
            }
        }
    }

  private:
    std::vector<std::pair<double, bool>> tokens_; // length and whether it is ink
    std::size_t current_ = 0;
    double left_ = 0;
};
} // namespace pd
