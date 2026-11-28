// SPDX-License-Identifier: LGPL-2.1-or-later
#include "qt_text.hpp"

#include <QFont>
#include <QPainterPath>
#include <QString>

namespace pd {
void qtTextStrokes(const Text& label, const StrokeSink& sink) {
    QFont font;
    font.setPixelSize(96); // font-independent DOM; fit actual system font ink into supplied local metrics
    QPainterPath path;
    path.addText(QPointF{}, font, QString::fromStdString(label.content()));
    const auto box = path.boundingRect();
    if (box.isEmpty()) {
        return;
    }
    auto ink = label.localInk();
    const auto span = ink.max() - ink.min();
    auto place = [&](QPointF p) {
        return label.placement().applyPoint({ink.min().x + (p.x() - box.left()) / box.width() * span.x,
                                             ink.min().y + (box.bottom() - p.y()) / box.height() * span.y});
    };
    // Subpaths, NOT fill polygons: never draw a bridge from a letter to its counter or the next letter.
    for (const auto& contour : path.toSubpathPolygons()) {
        for (qsizetype i = 1; i < contour.size(); ++i) {
            sink(Segment{place(contour[i - 1]), place(contour[i])});
        }
    }
}
} // namespace pd
