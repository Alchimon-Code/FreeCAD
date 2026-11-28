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
#include <QList>
#include <QScrollArea>
#include <QScrollBar>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QLabel>
#include <QMenu>
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
    // AutoCAD keeps the strip inside the window by folding a panel down to its name: the entries move
    // into the menu behind that name and the panel takes a fraction of the width. Nothing is lost —
    // the commands are one click away, and the panels that fit stay readable.
    void setParts(QWidget* body, QLabel* caption, QToolButton* fold) {
        body_ = body;
        caption_ = caption;
        fold_ = fold;
        if (fold_)
            fold_->hide();
    }
    bool canFold() const { return fold_ != nullptr; }
    bool isFolded() const { return folded_; }
    void setFolded(bool folded) {
        if (folded_ == folded || !canFold() || !body_)
            return;
        folded_ = folded;
        body_->setVisible(!folded);
        if (caption_)
            caption_->setVisible(!folded);
        fold_->setVisible(folded);
        updateGeometry();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        // AutoCAD puts no box around a ribbon panel: the strip is one surface and the only edge is a
        // hairline between two panels. The line leans away from the window colour in whichever
        // direction the theme needs, so it stays a hairline in light and in dark.
        QColor line = palette().color(QPalette::Window);
        line = line.lightness() < 128 ? line.lighter(150) : line.darker(125);
        QPainter painter(this);
        painter.setPen(line);
        painter.drawLine(width() - 1, 6, width() - 1, height() - 7);
    }

  private:
    QWidget* body_ = nullptr;
    QLabel* caption_ = nullptr;
    QToolButton* fold_ = nullptr;
    bool folded_ = false;
};

// The band behind the tabs and the quick access row is a shade apart from the page, not a colour of
// its own: a light theme wants a hair of shading, a dark one wants a good deal more, and a fixed
// darkening gives the light theme the heavy gray of a toolbar from another decade.
QColor bandColour(const QPalette& palette) {
    const QColor window = palette.color(QPalette::Window);
    return window.lightness() < 128 ? window.darker(122) : window.darker(106);
}

// The quick access bar keeps no background of its own: it rides on whatever strip the window puts it
// on — the menu row, as in AutoCAD's title strip — and draws no band that would cut that strip in two.
class QuickAccessBar final : public QToolBar {
  public:
    using QToolBar::QToolBar;
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
        back_->setToolTip(QCoreApplication::translate("Ribbon", "Scroll the ribbon panels left"));
        forward_->setToolTip(QCoreApplication::translate("Ribbon", "Scroll the ribbon panels right"));
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
        const QColor band = bandColour(palette());
        const QColor page = palette().color(QPalette::Window);
        const QColor ink = palette().color(QPalette::WindowText);
        painter.fillRect(rect(), band);
        // AutoCAD's tabs carry no outline at all: the current one is a patch of the page colour that
        // flows into the panels below, and the rest are labels on the darker band.
        for (int i = 0; i < count(); ++i) {
            const QRect cell = tabRect(i);
            if (cell.isEmpty())
                continue;
            const bool current = i == currentIndex();
            QColor text = ink;
            if (current) {
                painter.fillRect(cell, page);
            } else {
                text.setAlpha(170);
            }
            painter.setPen(text);
            const int pad = 8;
            const QString caption =
                fontMetrics().elidedText(tabText(i), Qt::ElideRight, std::max(0, cell.width() - 2 * pad));
            painter.drawText(cell.adjusted(pad, 0, -pad, 0), Qt::AlignCenter, caption);
        }
    }

  private:
    QToolButton* back_ = nullptr;
    QToolButton* forward_ = nullptr;
};

QFrame* panelFrame(const QString& title, QLabel** out, QWidget* parent) {
    auto* frame = new PanelFrame(parent);
    // Every panel takes the height of the tallest one on its page. The buttons stay at the top
    // because the frame keeps a stretch above its caption, so a two-row grid never spreads; what
    // moves is the caption, which lands on one baseline across the strip the way AutoCAD lines
    // them up.
    frame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(8, 2, 8, 0);
    box->setSpacing(1);
    box->addStretch(1);
    auto* label = new QLabel(QCoreApplication::translate("MainWindow", title.toUtf8().constData()), frame);
    label->setObjectName("ribbonPanelTitle");
    label->setProperty("titleSource", title);
    label->setAlignment(Qt::AlignHCenter);
    QFont titleFont = label->font();
    titleFont.setPixelSize(9);
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
    // A small entry is one line of text beside a 16 px icon. The style reserves a button's worth of
    // air around that line, and three rows of it is a dozen pixels the drawing never gets back, so a
    // small entry is held to the line itself.
    if (beside)
        button->setFixedHeight(std::min(hint.height(), button->fontMetrics().height() + 6));
}

// A scroll area sizes its page to the page's hint, and a page whose panels need more than that hint
// is clipped at the right: the panels beyond it vanish and a blank strip sits where the next one
// should start. A page answers with the size its layout asks for, so every panel stays inside it.
class PageBody final : public QWidget {
  public:
    explicit PageBody(QWidget* parent = nullptr) : QWidget(parent) {
        // Minimum, not Preferred: the hint is then the floor the scroll area may not go under, and a
        // page still grows to fill a wider window.
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override {
        const auto* box = layout();
        return box ? box->sizeHint() : QWidget::sizeHint();
    }
    QSize minimumSizeHint() const override {
        const auto* box = layout();
        return box ? box->minimumSize() : QWidget::minimumSizeHint();
    }
};

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
        return {240, height};
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
    quickAccess_ = new QuickAccessBar(this);
    quickAccess_->setObjectName("quickAccess");
    quickAccess_->setIconSize({18, 18});
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
    auto* widget = new PageBody(pages_);
    auto* row = new QHBoxLayout(widget);
    row->setContentsMargins(3, 2, 3, 2);
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
    // A page that starts to overflow, or stops, has to move the chevrons with it.
    connect(scroll->horizontalScrollBar(), &QScrollBar::rangeChanged, this,
            [this] { syncChevrons(); });
}

QWidget* Ribbon::page(const QString& key) {
    if (!index_.contains(key))
        addPage(key);
    auto* widget = rows_.value(key);
    return widget ? widget : pages_->widget(index_[key]);
}

// The name of a folded panel is a button, and the button carries the entries of that panel.
static QToolButton* foldButton(const QString& title, const QList<QAction*>& actions, QWidget* parent) {
    auto* fold = new QToolButton(parent);
    fold->setObjectName("ribbonPanelFold");
    fold->setProperty("titleSource", title);
    fold->setText(QCoreApplication::translate("MainWindow", title.toUtf8().constData()));
    fold->setToolButtonStyle(Qt::ToolButtonTextOnly);
    fold->setPopupMode(QToolButton::InstantPopup);
    fold->setAutoRaise(true);
    auto* menu = new QMenu(fold);
    for (auto* action : actions)
        menu->addAction(action);
    fold->setMenu(menu);
    fold->hide();
    return fold;
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
    // The air between a large command and the grid of small ones, and between two stacked large
    // commands, is what makes a panel read as a group rather than as a wall of buttons.
    strip->setSpacing(8);
    auto* leaders = new QVBoxLayout;
    leaders->setContentsMargins(0, 0, 0, 0);
    leaders->setSpacing(4);
    // The first `primary` actions of a panel are its large commands, the rest are small entries.
    const int first = std::clamp(primary, 0, int(actions.size()));
    for (int i = 0; i < first; ++i)
        leaders->addWidget(ribbonButton(actions[i], buttons, true));
    leaders->addStretch(1);
    strip->addLayout(leaders);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(0);
    columns = std::max(1, columns);
    int rowHeight = 0;
    for (int i = first; i < actions.size(); ++i) {
        auto* button = ribbonButton(actions[i], buttons, false);
        grid->addWidget(button, (i - first) / columns, (i - first) % columns);
        // The held height, not the style's hint: the grid has to leave room for what is drawn.
        rowHeight = std::max(rowHeight, button->minimumHeight());
    }
    const int small = int(actions.size()) - first;
    const int rows = (small + columns - 1) / columns;
    grid->setRowStretch(rows, 1);
    strip->addLayout(grid);
    if (rows > 0)
        buttons->setMinimumHeight(rows * rowHeight + (rows - 1) * grid->spacing());
    auto* box = qobject_cast<QVBoxLayout*>(frame->layout());
    box->insertWidget(0, buttons);
    auto* fold = foldButton(title, actions, frame);
    box->insertWidget(1, fold);
    auto* panel = static_cast<PanelFrame*>(frame);
    panel->setParts(buttons, label, fold);
    panels_[pageKey].append(panel);
    row->insertWidget(row->count() - 1, frame);
    panelLabels_.append(label);
    evenColumns();
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
    row->insertWidget(row->count() - 1, frame);
    panelLabels_.append(label);
}

// The quick access toolbar rides on the window's title strip. Taking it out of the ribbon's column is
// what stops the band from reserving a row for a toolbar that is no longer in it: the row came to
// about the height of the toolbar, and the drawing area paid for it on every tab.
QToolBar* Ribbon::takeQuickAccess() {
    if (!quickAccess_)
        return nullptr;
    if (auto* box = qobject_cast<QVBoxLayout*>(layout()))
        box->removeWidget(quickAccess_);
    quickAccess_->setParent(nullptr);
    return quickAccess_;
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
    for (auto* fold : pages_->findChildren<QToolButton*>(QStringLiteral("ribbonPanelFold"))) {
        const auto source = fold->property("titleSource").toString();
        fold->setText(QCoreApplication::translate("MainWindow", source.toUtf8().constData()));
    }
    applyPageTitles();
    // The captions are translated after the buttons were measured, and a Russian name can be a good
    // deal longer than the English one it was measured with. Ask again, or the button clips the
    // caption mid word instead of eliding it.
    for (auto* button : pages_->findChildren<QToolButton*>()) {
        if (button->toolButtonStyle() != Qt::ToolButtonIconOnly)
            fitCaption(button);
    }
    evenColumns();
    updateStrip();
}

// A panel of small buttons is a grid: the entries of one column start at the same x, so the captions
// line up instead of stepping in and out with their lengths. Each button keeps its own width and the
// column takes the widest one, which is measured again whenever the captions change.
void Ribbon::evenColumns() {
    for (auto* grid : pages_->findChildren<QGridLayout*>()) {
        QList<int> widest;
        for (int i = 0; i < grid->count(); ++i) {
            auto* button = qobject_cast<QToolButton*>(grid->itemAt(i)->widget());
            if (!button)
                continue;
            int row = 0;
            int column = 0;
            int rowSpan = 1;
            int columnSpan = 1;
            grid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
            if (column >= int(widest.size()))
                widest.resize(column + 1, 0);
            widest[column] = std::max(widest[column], button->minimumWidth());
        }
        for (int column = 0; column < int(widest.size()); ++column)
            grid->setColumnMinimumWidth(column, widest[column]);
    }
}

void Ribbon::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // The window opens at one size and is given another a moment later; the pages were measured
    // against the first one and keep that geometry, which is how a strip comes up three panels wide
    // while the tabs are already in place.
    if (auto* scroll = static_cast<PageScroll*>(pages_->currentWidget())) {
        if (QWidget* page = scroll->widget()) {
            page->adjustSize();
            page->updateGeometry();
        }
    }
    syncChevrons();
}

// The first frame is laid out here: the chevrons say whether the page is wider than the strip, and
// until now they only followed a tab change or a window resize, so a fresh window showed the first
// panels of a page it could not scroll.
void Ribbon::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateStrip();
    // A page measured before the window had its size keeps that geometry until something asks again.
    if (auto* scroll = static_cast<PageScroll*>(pages_->currentWidget())) {
        if (QWidget* page = scroll->widget()) {
            page->adjustSize();
            page->updateGeometry();
        }
        scroll->horizontalScrollBar()->setValue(0);
    }
    syncChevrons();
}


// A ribbon that sits on a toolbar has to nudge it; a ribbon asked to stand on its own is left alone.
void Ribbon::askBandForRoom() {
    auto* bar = qobject_cast<QToolBar*>(parentWidget());
    if (!bar)
        return;
    // The band is asked to re-measure, never told a height: a fixed height taken from the widgets'
    // current geometry feeds back into the layout and folds the ribbon shut.
    bar->layout()->invalidate();
    bar->updateGeometry();
}

void Ribbon::setCollapsed(bool collapsed) {
    if (collapsed_ == collapsed)
        return;
    collapsed_ = collapsed;
    pages_->setVisible(!collapsed_);
    updateGeometry();
    askBandForRoom();
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

// A ribbon never cuts a panel in half: when the strip is narrower than its panels, the last ones fold
// down to their names, the way AutoCAD shrinks a panel that no longer fits.
void Ribbon::fitPanels() {
    if (fitting_)
        return;
    // Folding changes the height the band asks for, which comes back as a resize; the flag keeps that
    // answer from re-entering the question.
    fitting_ = true;
    struct Settled {
        bool& flag;
        ~Settled() { flag = false; }
    } settled{fitting_};
    auto* scroll = static_cast<PageScroll*>(pages_->currentWidget());
    if (!scroll || !scroll->widget())
        return;
    auto* row = qobject_cast<QHBoxLayout*>(scroll->widget()->layout());
    if (!row)
        return;
    const int page = pages_->currentIndex();
    const QString key = index_.key(page);
    auto& frames = panels_[key];
    for (auto* widget : frames)
        static_cast<PanelFrame*>(widget)->setFolded(false);
    row->activate();
    const int room = scroll->viewport()->width();
    for (int i = int(frames.size()) - 1; i >= 0 && row->minimumSize().width() > room; --i)
        if (auto* frame = static_cast<PanelFrame*>(frames[i]); frame->canFold()) {
            frame->setFolded(true);
            row->activate();
        }
    if (QWidget* content = scroll->widget())
        content->updateGeometry();
}

void Ribbon::updateStrip() {
    // Captions arrive after the panels are built, so the strip is stated here instead of waiting for a
    // chain of cached layout hints to catch up. The height comes from the tallest page rather than
    // from the open one: AutoCAD's ribbon keeps one height, so switching tabs does not move the
    // drawing area by a pixel.
    if (collapsed_)
        return;
    fitPanels();
    int need = 0;
    for (int i = 0; i < pages_->count(); ++i) {
        auto* scroll = qobject_cast<QScrollArea*>(pages_->widget(i));
        if (!scroll || !scroll->widget())
            continue;
        const QWidget* content = scroll->widget();
        need = std::max({need, content->sizeHint().height(), content->minimumSizeHint().height()});
    }
    if (need > 0)
        pages_->setFixedHeight(need);
    // The band is a widget action on a toolbar, and a toolbar caches the height of its items: a page
    // that needs more room has to say so before the tabs can grow.
    askBandForRoom();
    syncChevrons();
}

void Ribbon::refreshLayers(const QStringList& names, const QString& current) {
    const QSignalBlocker guard(layer_);
    layer_->clear();
    layer_->addItems(names);
    const int found = current.isEmpty() ? -1 : layer_->findText(current);
    layer_->setCurrentIndex(found >= 0 ? found : (layer_->count() ? 0 : -1));
}
