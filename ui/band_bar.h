// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QStyle>
#include <QToolBar>
#include <QWidgetAction>

// The band the ribbon rides on. A plain toolbar takes its height from the style's tool bar metrics,
// which came to about twenty-five pixels more than the ribbon needs — the drawing area paid for them
// on every tab. This one states its height as the ribbon's own plus the frame the style draws around
// an item, so the band and its ribbon cannot drift apart. The height is asked from the size hints and
// never from the current geometry: a band measured from its own layout folds the ribbon shut.
class BandBar : public QToolBar {
  public:
    explicit BandBar(QWidget* parent = nullptr) : QToolBar(parent) {}
    QSize sizeHint() const override {
        QSize hint = QToolBar::sizeHint();
        if (const QWidget* ribbon = carried())
            hint.setHeight(ribbon->sizeHint().height() + 2 * frame());
        return hint;
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

  private:
    const QWidget* carried() const {
        // widgetForAction takes its action by pointer to non-const, and this question is asked from a
        // const size hint.
        auto* action = actions().value(0);
        return action ? const_cast<BandBar*>(this)->widgetForAction(action) : nullptr;
    }
    int frame() const {
        const auto* metrics = style();
        return metrics->pixelMetric(QStyle::PM_ToolBarFrameWidth, nullptr, this) +
               metrics->pixelMetric(QStyle::PM_ToolBarItemMargin, nullptr, this);
    }
};
