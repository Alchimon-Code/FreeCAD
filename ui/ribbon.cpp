// SPDX-License-Identifier: LGPL-2.1-or-later
#include "ribbon.h"

#include <QAction>
#include <QColor>
#include <QCoreApplication>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QLabel>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QToolButton>
#include <QIcon>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {
// AutoCAD frames every ribbon panel as its own group with the caption inside the frame, and the tabs
// sit on a darker band with the active tab merging into the page below. All of it is painted from the
// palette instead of a style-sheet rule: a style sheet on the frame freezes the palettes of the
// buttons it polishes, so switching to the light theme left every caption in the dark theme's text
// colour — visible only as a nearly invisible ribbon.
class PanelFrame : public QFrame {
  public:
    explicit PanelFrame(QWidget* parent = nullptr) : QFrame(parent) {
        setFrameShape(QFrame::NoFrame);
        setObjectName("ribbonPanel");
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRect group = rect().adjusted(0, 0, -1, -1);
        painter.fillRect(group, palette().color(QPalette::Base));
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(group);
    }
};

// The tab strip: a darker band with the current tab painted in the page colour, so the two read as
// one surface the way they do in AutoCAD, instead of a browser-style tab with an accent bar under it.
class RibbonTabBar : public QTabBar {
  public:
    using QTabBar::QTabBar;
    // Double-clicking a tab minimizes the ribbon to its tabs, exactly as it does in AutoCAD.
    std::function<void()> onDoubleClick;
    // AutoCAD scrolls the panels with two chevrons at the right end of the tab band instead of a
    // scroll bar under the strip, so the two live here and ride on the band.
    std::function<void(int)> onScroll;
    void attachChevrons(QToolButton* back, QToolButton* forward) {
        back_ = back;
        forward_ = forward;
        for (auto* button : {back_, forward_}) {
            button->setParent(this);
            button->setAutoRaise(true);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setFocusPolicy(Qt::NoFocus);
        }
        // Thin angle brackets, the way AutoCAD draws them: the stock arrow icons are a dark pixel
        // mesh at this size on the tab band and read as two dots.
        QFont chevron = font();
        chevron.setPixelSize(13);
        chevron.setBold(true);
        for (auto* button : {back_, forward_}) {
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setFont(chevron);
        }
        back_->setText(QStringLiteral("<"));
        forward_->setText(QStringLiteral(">"));
        back_->setToolTip(tr("Scroll the ribbon panels left"));
        forward_->setToolTip(tr("Scroll the ribbon panels right"));
        back_->setObjectName("ribbonScrollLeft");
        forward_->setObjectName("ribbonScrollRight");
        QObject::connect(back_, &QToolButton::clicked, this, [this] {
            if (onScroll)
                onScroll(-1);
        });
        QObject::connect(forward_, &QToolButton::clicked, this, [this] {
            if (onScroll)
                onScroll(1);
        });
        placeChevrons();
    }
    void setChevronsEnabled(bool back, bool forward) {
        if (back_)
            back_->setEnabled(back);
        if (forward_)
            forward_->setEnabled(forward);
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QTabBar::resizeEvent(event);
        placeChevrons();
    }
    void placeChevrons() {
        if (!back_ || !forward_)
            return;
        const int side = std::max(14, height() - 6);
        const int y = (height() - side) / 2;
        forward_->setGeometry(width() - side - 3, y, side, side);
        back_->setGeometry(width() - 2 * side - 5, y, side, side);
        back_->raise();
        forward_->raise();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (onDoubleClick && event->button() == Qt::LeftButton) {
            onDoubleClick();
            return;
        }
        QTabBar::mouseDoubleClickEvent(event);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QColor band = palette().color(QPalette::Window).darker(108);
        const QColor page = palette().color(QPalette::Window);
        const QColor ink = palette().color(QPalette::WindowText);
        const QColor edge = palette().color(QPalette::Mid);
        painter.fillRect(rect(), band);
        for (int i = 0; i < count(); ++i) {
            const QRect cell = tabRect(i);
            if (cell.isEmpty())
                continue;
            if (i == currentIndex()) {
                // The current tab merges into the page: no bottom edge, an accent line on top.
                const QRect surface = cell.adjusted(0, 2, 0, 0);
                painter.fillRect(surface, page);
                painter.setPen(edge);
                painter.drawLine(surface.topLeft(), surface.topRight());
                painter.drawLine(surface.topLeft(), surface.bottomLeft());
                painter.drawLine(surface.topRight(), surface.bottomRight());
                painter.setPen(palette().color(QPalette::Highlight));
                painter.drawLine(surface.left(), surface.top(), surface.right(), surface.top());
            } else {
                painter.setPen(edge);
                painter.drawLine(cell.topRight(), cell.bottomRight());
            }
            painter.setPen(ink);
            painter.drawText(cell, Qt::AlignCenter, tabText(i));
        }
    }

  private:
    QToolButton* back_ = nullptr;
    QToolButton* forward_ = nullptr;
};

QFrame* panelFrame(const QString& title, QLabel** out, QWidget* parent) {
    auto* frame = new PanelFrame(parent);
    // A short page (Insert has three small panels) must keep its buttons at the top: stretching the
    // frame spreads a two-row grid over the whole strip, which reads as a broken layout.
    frame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(7, 5, 7, 3);
    box->setSpacing(2);
    box->addStretch(1);
    auto* label = new QLabel(QCoreApplication::translate("MainWindow", title.toUtf8().constData()), frame);
    label->setObjectName("ribbonPanelTitle");
    label->setProperty("titleSource", title);
    label->setAlignment(Qt::AlignHCenter);
    QFont titleFont = label->font();
    titleFont.setPixelSize(10);
    label->setFont(titleFont);
    label->setForegroundRole(QPalette::Dark);
    box->addWidget(label);
    *out = label;
    return frame;
}
// Fit the whole command name instead of eliding it to "Сопр...": a CAD ribbon must be readable
// without hovering. Captions are translated after the panel exists, so the same helper runs again
// whenever the action changes.
void fitCaption(QToolButton* button) {
    // The style elides against the label rectangle it derives from the button margins, and those
    // margins are larger than the size hint reserves. Both are measured here, so the caption is
    // painted in full instead of as "Полилиния...".
    const auto* style = button->style();
    const int margins = 2 * style->pixelMetric(QStyle::PM_ButtonMargin, nullptr, button) +
                        2 * style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, button);
    const bool beside = button->toolButtonStyle() == Qt::ToolButtonTextBesideIcon;
    // A button whose caption is still empty reports the height of an icon-only button, so a panel
    // built before the translations arrive would keep a too short strip. The size of a real caption
    // is asked from the style instead: one line costs the same whatever it says.
    QStyleOptionToolButton opt;
    opt.initFrom(button);
    opt.icon = button->icon();
    opt.iconSize = button->iconSize();
    const bool empty = button->text().isEmpty();
    opt.text = empty ? QStringLiteral("Wq") : button->text();
    opt.toolButtonStyle = button->toolButtonStyle();
    const QSize hint = style->sizeFromContents(QStyle::CT_ToolButton, &opt, button->iconSize(), button);
    // Fusion keeps about thirty pixels of its own inside a text-under-icon button, a few more than
    // the margins it reports, so the reserve carries a little air: an extra pixel here costs nothing,
    // an elided caption costs the user a tooltip on every command.
    const int caption = button->fontMetrics().horizontalAdvance(opt.text);
    const int needed = std::max(hint.width(), caption + margins + (beside ? 10 : 24));
    button->setMinimumWidth(needed);
    button->setMaximumWidth(needed);
    button->setMinimumHeight(hint.height());
}

// A ribbon page is a fixed strip. Its height is the tallest panel, so a third row of buttons and the
// panel captions under it are never cut off; the extra scrollbar extent keeps the text visible when
// a horizontal bar appears. The width hint stays small: the page scrolls, the window does not grow.
class PageScroll : public QScrollArea {
  public:
    explicit PageScroll(QWidget* parent = nullptr) : QScrollArea(parent) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    // The chevrons ask for a direction, the wheel for a distance: both end up sliding the page.
    void slide(int direction) { slideBy(direction * 180); }
    void slideBy(int delta) {
        auto* bar = horizontalScrollBar();
        bar->setValue(bar->value() + delta);
    }
    QSize sizeHint() const override {
        const QWidget* content = widget();
        if (!content)
            return QScrollArea::sizeHint();
        // The grid hint of a row of fixed-width buttons comes out short on some styles, so the page
        // also states how tall it has to be; taking both keeps a per-tab ribbon unclipped.
        const int height = std::max(content->sizeHint().height(), content->minimumSizeHint().height());
        return {240, height + 4};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

  protected:
    void wheelEvent(QWheelEvent* event) override {
        auto* bar = horizontalScrollBar();
        if (bar->maximum() <= 0) {
            QScrollArea::wheelEvent(event);
            return;
        }
        // AutoCAD scrolls the panels of the ribbon with the wheel when they do not fit.
        const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
        slideBy(-delta);
        event->accept();
    }
};
// AutoCAD's ribbon is as tall as the tab on screen: Home fills three rows, Insert needs one, and the
// strip follows. A plain QStackedWidget reports the tallest page instead, which leaves a block of
// empty ribbon under every short tab.
class PageStack : public QStackedWidget {
  public:
    using QStackedWidget::QStackedWidget;
    QSize sizeHint() const override {
        const QWidget* current = currentWidget();
        return current ? current->sizeHint() : QStackedWidget::sizeHint();
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
};

// AutoCAD's panels lead with one large command and list the rest as small entries beside it. The
// large one carries the caption under a 26 px icon; a small entry keeps the caption beside a 16 px
// icon, which is what lets nine panels share the Home tab without scrolling.
QToolButton* ribbonButton(QAction* action, QWidget* parent, bool large = true) {
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(large ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    button->setIconSize(large ? QSize(26, 26) : QSize(16, 16));
    button->setToolTip(action->toolTip());
    QFont font = button->font();
    font.setPointSizeF(std::max(7.5, font.pointSizeF() - 1.0));
    button->setFont(font);
    fitCaption(button);
    QObject::connect(action, &QAction::changed, button, [button] { fitCaption(button); });
    return button;
}
const QColor kColors[] = {QColor("#e6403a"), QColor("#f0a63a"), QColor("#f5e14a"), QColor("#54c46a"),
                          QColor("#3fa8e0"), QColor("#7a63d7"), QColor("#ffffff")};
} // namespace

Ribbon::Ribbon(QWidget* parent) : QWidget(parent) {
    setObjectName("ribbon");
    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(0);
    quickAccess_ = new QToolBar(this);
    quickAccess_->setObjectName("quickAccess");
    quickAccess_->setIconSize({20, 20});
    quickAccess_->setMovable(false);
    quickAccess_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    box->addWidget(quickAccess_);
    tabs_ = new RibbonTabBar(this);
    tabs_->setObjectName("ribbonTabs");
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    box->addWidget(tabs_);
    pages_ = new PageStack(this);
    pages_->setObjectName("ribbonPages");
    pages_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // The page strip takes the window colour and the panels are drawn as their own groups on it, so
    // the ribbon reads as a band of framed commands rather than a row of floating buttons.
    pages_->setBackgroundRole(QPalette::Window);
    pages_->setAutoFillBackground(true);
    box->addWidget(pages_);
    connect(tabs_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);
    // The strip is sized by the page on screen, so a tab change has to re-ask the layout.
    connect(tabs_, &QTabBar::currentChanged, this, [this] {
        updateStrip();
        syncChevrons();
    });
    static_cast<RibbonTabBar*>(tabs_)->onDoubleClick = [this] { setCollapsed(!collapsed_); };
    auto* bar = static_cast<RibbonTabBar*>(tabs_);
    bar->attachChevrons(new QToolButton(tabs_), new QToolButton(tabs_));
    bar->onScroll = [this](int direction) {
        // Every page of the stack is a PageScroll, and the class carries no Q_OBJECT of its own.
        if (auto* scroll = static_cast<PageScroll*>(pages_->currentWidget()))
            scroll->slide(direction);
    };

    layer_ = new QComboBox(this);
    layer_->setObjectName("ribbonLayer");
    layer_->setMinimumWidth(150);
    layer_->setToolTip(tr("Current layer of the selection"));
    color_ = new QComboBox(this);
    color_->setObjectName("ribbonColor");
    color_->setMinimumWidth(110);
    color_->addItem(tr("By layer"), QStringLiteral("ByLayer"));
    color_->addItem(tr("By block"), QStringLiteral("ByBlock"));
    for (auto c : kColors) {
        color_->addItem(QIcon(), c.name(), c);
        color_->setItemData(color_->count() - 1, c, Qt::DecorationRole);
    }
    weight_ = new QComboBox(this);
    weight_->setObjectName("ribbonWeight");
    weight_->setMinimumWidth(110);
    weight_->addItem(tr("By layer"), QStringLiteral("ByLayer"));
    weight_->addItem(tr("By block"), QStringLiteral("ByBlock"));
    for (double mm : {0.05, 0.09, 0.13, 0.18, 0.25, 0.35, 0.5, 0.7, 1.0, 1.4, 2.0})
        weight_->addItem(QStringLiteral("%1 mm").arg(mm, 0, 'f', 2), mm);
    connect(layer_, &QComboBox::activated, this,
            [this](int) { emit layerPicked(layer_->currentText()); });
    connect(color_, &QComboBox::activated, this,
            [this](int) { emit colorPicked(color_->currentData()); });
    connect(weight_, &QComboBox::activated, this,
            [this](int) { emit weightPicked(weight_->currentData()); });
}

void Ribbon::addPage(const QString& key) {
    if (index_.contains(key))
        return;
    index_[key] = tabs_->addTab(QString());
    auto* widget = new QWidget(pages_);
    auto* row = new QHBoxLayout(widget);
    row->setContentsMargins(3, 3, 3, 3);
    row->setSpacing(3);
    row->addStretch(1);
    // A page keeps its natural width instead of squeezing panel buttons until their captions
    // elide to "От..."; a narrow window scrolls, which is what a real ribbon does too.
    auto* scroll = new PageScroll(pages_);
    scroll->setObjectName("ribbonScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->viewport()->setBackgroundRole(QPalette::Window);
    scroll->setWidget(widget);
    pages_->addWidget(scroll);
    rows_.insert(key, widget);
    connect(scroll->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { syncChevrons(); });
}

QWidget* Ribbon::page(const QString& key) {
    if (!index_.contains(key))
        addPage(key);
    auto* widget = rows_.value(key);
    return widget ? widget : pages_->widget(index_[key]);
}

void Ribbon::addPanel(const QString& pageKey, const QString& title, const QList<QAction*>& actions,
                      int columns, int primary) {
    auto* target = page(pageKey);
    auto* row = qobject_cast<QHBoxLayout*>(target->layout());
    QLabel* label = nullptr;
    auto* frame = panelFrame(title, &label, target);
    auto* buttons = new QWidget(frame);
    auto* strip = new QHBoxLayout(buttons);
    strip->setContentsMargins(0, 0, 0, 0);
    strip->setSpacing(4);
    auto* leaders = new QVBoxLayout;
    leaders->setContentsMargins(0, 0, 0, 0);
    leaders->setSpacing(2);
    // The first `primary` actions of a panel are its large commands, the rest are small entries.
    const int first = std::clamp(primary, 0, int(actions.size()));
    for (int i = 0; i < first; ++i)
        leaders->addWidget(ribbonButton(actions[i], buttons, true));
    leaders->addStretch(1);
    strip->addLayout(leaders);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    columns = std::max(1, columns);
    int rowHeight = 0;
    for (int i = first; i < actions.size(); ++i) {
        auto* button = ribbonButton(actions[i], buttons, false);
        grid->addWidget(button, (i - first) / columns, (i - first) % columns);
        rowHeight = std::max(rowHeight, button->sizeHint().height());
    }
    const int small = int(actions.size()) - first;
    const int rows = (small + columns - 1) / columns;
    grid->setRowStretch(rows, 1);
    strip->addLayout(grid);
    if (rows > 0)
        buttons->setMinimumHeight(rows * rowHeight + (rows - 1) * grid->spacing());
    auto* box = qobject_cast<QVBoxLayout*>(frame->layout());
    box->insertWidget(0, buttons);
    row->insertWidget(row->count() - 1, frame, 0, Qt::AlignTop);
    panelLabels_.append(label);
}

void Ribbon::addPanelWidget(const QString& pageKey, const QString& title, QWidget* widget) {
    auto* target = page(pageKey);
    auto* row = qobject_cast<QHBoxLayout*>(target->layout());
    QLabel* label = nullptr;
    auto* frame = panelFrame(title, &label, target);
    auto* holder = new QWidget(frame);
    holder->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* vbox = new QVBoxLayout(holder);
    vbox->setContentsMargins(0, 2, 0, 2);
    vbox->addWidget(widget);
    holder->setMinimumHeight(widget->sizeHint().height() + 4);
    auto* box = qobject_cast<QVBoxLayout*>(frame->layout());
    box->insertWidget(0, holder);
    row->insertWidget(row->count() - 1, frame, 0, Qt::AlignTop);
    panelLabels_.append(label);
}

void Ribbon::setQuickAccess(const QList<QAction*>& actions) {
    quickAccess_->clear();
    for (auto* action : actions) {
        if (!action)
            continue;
        if (action->isSeparator())
            quickAccess_->addSeparator();
        else
            quickAccess_->addAction(action);
    }
}

void Ribbon::setCurrentPage(int index) {
    if (index >= 0 && index < tabs_->count())
        tabs_->setCurrentIndex(index);
}
int Ribbon::currentPage() const { return tabs_->currentIndex(); }

void Ribbon::setPageTitles(const QStringList& sources) {
    pageSources_ = sources;
    applyPageTitles();
}

void Ribbon::applyPageTitles() {
    for (int i = 0; i < tabs_->count() && i < pageSources_.size(); ++i)
        tabs_->setTabText(i, QCoreApplication::translate("MainWindow", pageSources_[i].toUtf8().constData()));
}

void Ribbon::retranslate() {
    for (auto* label : panelLabels_) {
        if (!label)
            continue;
        const auto source = label->property("titleSource").toString();
        label->setText(QCoreApplication::translate("MainWindow", source.toUtf8().constData()));
    }
    applyPageTitles();
    updateStrip();
}

void Ribbon::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    syncChevrons();
}

void Ribbon::setCollapsed(bool collapsed) {
    if (collapsed_ == collapsed)
        return;
    collapsed_ = collapsed;
    pages_->setVisible(!collapsed_);
    updateGeometry();
    if (auto* window = parentWidget())
        window->updateGeometry();
}

// A page that fits its viewport has nothing to scroll, and the chevrons say so by going grey.
void Ribbon::syncChevrons() {
    auto* bar = static_cast<RibbonTabBar*>(tabs_);
    auto* scroll = collapsed_ ? nullptr : static_cast<PageScroll*>(pages_->currentWidget());
    if (!scroll) {
        bar->setChevronsEnabled(false, false);
        return;
    }
    auto* horizontal = scroll->horizontalScrollBar();
    bar->setChevronsEnabled(horizontal->value() > horizontal->minimum(),
                            horizontal->value() < horizontal->maximum());
}

void Ribbon::updateStrip() {
    // Captions arrive after the panels are built, so the strip is stated here, from the page the user
    // is looking at, instead of waiting for a chain of cached layout hints to catch up.
    if (collapsed_)
        return;
    auto* scroll = qobject_cast<QScrollArea*>(pages_->currentWidget());
    if (!scroll)
        return;
    const QWidget* content = scroll->widget();
    if (!content)
        return;
    const int need = std::max(content->sizeHint().height(), content->minimumSizeHint().height());
    if (need > 0)
        pages_->setFixedHeight(need + 4);
    syncChevrons();
}

void Ribbon::refreshLayers(const QStringList& names, const QString& current) {
    const QSignalBlocker guard(layer_);
    layer_->clear();
    layer_->addItems(names);
    const int found = current.isEmpty() ? -1 : layer_->findText(current);
    layer_->setCurrentIndex(found >= 0 ? found : (layer_->count() ? 0 : -1));
}
