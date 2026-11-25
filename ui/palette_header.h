// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QHeaderView>
#include <QPainter>

// AutoCAD's palette headers are a neutral strip with a hairline under it, not a slab of the accent
// colour the style paints them with. Both docks put their columns under this header, which draws the
// sections from the palette alone so it follows the theme.
class PaletteHeader : public QHeaderView {
  public:
    explicit PaletteHeader(QWidget* parent = nullptr) : QHeaderView(Qt::Horizontal, parent) {
        setSectionsClickable(false);
        setHighlightSections(false);
    }

  protected:
    void paintSection(QPainter* painter, const QRect& rect, int index) const override {
        const QColor window = palette().color(QPalette::Window);
        const bool dark = window.lightness() < 128;
        painter->fillRect(rect, dark ? window.lighter(118) : window.darker(106));
        painter->setPen(dark ? window.lighter(150) : window.darker(125));
        painter->drawLine(rect.topRight(), rect.bottomRight());
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());
        const QString text = model() ? model()->headerData(index, Qt::Horizontal).toString() : QString();
        painter->setPen(palette().color(QPalette::WindowText));
        painter->drawText(rect.adjusted(6, 0, -6, 0), defaultAlignment(), text);
    }
};
