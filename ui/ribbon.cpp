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
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QLabel>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QIcon>
#include <QVBoxLayout>

#include <algorithm>

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

  protected:
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
};

QFrame* panelFrame(const QString& title, QLabel** out, QWidget* parent) {
    auto* frame = new PanelFrame(parent);
    // A short page (Insert has three small panels) must keep its buttons at the top: stretching the
    // frame spreads a two-row grid over the whole strip, which reads as a broken layout.
    frame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(9, 6, 9, 4);
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
    const int caption = button->fontMetrics().horizontalAdvance(button->text());
    const int needed = std::max(54, caption + margins + 14);
    button->setMinimumWidth(needed);
    button->setMaximumWidth(needed);
}

// A ribbon page is a fixed strip. Its height is the tallest panel, so a third row of buttons and the
// panel captions under it are never cut off; the extra scrollbar extent keeps the text visible when
// a horizontal bar appears. The width hint stays small: the page scrolls, the window does not grow.
class PageScroll : public QScrollArea {
  public:
    using QScrollArea::QScrollArea;
    QSize sizeHint() const override {
        const QWidget* content = widget();
        if (!content)
            return QScrollArea::sizeHint();
        return {240, content->sizeHint().height() + 24};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
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

QToolButton* ribbonButton(QAction* action, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setAutoRaise(true);
    button->setIconSize({26, 26});
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
    connect(tabs_, &QTabBar::currentChanged, this, [this] { pages_->updateGeometry(); });

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
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->viewport()->setBackgroundRole(QPalette::Window);
    scroll->setWidget(widget);
    pages_->addWidget(scroll);
    rows_.insert(key, widget);
}

QWidget* Ribbon::page(const QString& key) {
    if (!index_.contains(key))
        addPage(key);
    auto* widget = rows_.value(key);
    return widget ? widget : pages_->widget(index_[key]);
}

void Ribbon::addPanel(const QString& pageKey, const QString& title, const QList<QAction*>& actions,
                      int columns) {
    auto* target = page(pageKey);
    auto* row = qobject_cast<QHBoxLayout*>(target->layout());
    QLabel* label = nullptr;
    auto* frame = panelFrame(title, &label, target);
    auto* buttons = new QWidget(frame);
    auto* grid = new QGridLayout(buttons);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    columns = std::max(1, columns);
    for (int i = 0; i < actions.size(); ++i)
        grid->addWidget(ribbonButton(actions[i], buttons), i / columns, i % columns);
    grid->setRowStretch((actions.size() + columns - 1) / columns, 1);
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
}

void Ribbon::refreshLayers(const QStringList& names, const QString& current) {
    const QSignalBlocker guard(layer_);
    layer_->clear();
    layer_->addItems(names);
    const int found = current.isEmpty() ? -1 : layer_->findText(current);
    layer_->setCurrentIndex(found >= 0 ? found : (layer_->count() ? 0 : -1));
}
