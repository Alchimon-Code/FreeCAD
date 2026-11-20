// SPDX-License-Identifier: LGPL-2.1-or-later
#include "ribbon.h"

#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolBar>
#include <QSignalBlocker>
#include <QToolButton>
#include <QIcon>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QFrame* panelFrame(const QString& title, QLabel** out, QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setFrameShape(QFrame::NoFrame);
    frame->setObjectName("ribbonPanel");
    frame->setStyleSheet("#ribbonPanel { border-right: 1px solid palette(mid); }");
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(8, 4, 8, 2);
    box->setSpacing(2);
    box->addStretch(1);
    auto* label = new QLabel(title, frame);
    label->setObjectName("ribbonPanelTitle");
    label->setAlignment(Qt::AlignHCenter);
    label->setStyleSheet("font-size: 10px; color: palette(dark);");
    box->addWidget(label);
    *out = label;
    return frame;
}
QToolButton* ribbonButton(QAction* action, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setAutoRaise(true);
    button->setIconSize({22, 22});
    // Fit the whole command name instead of eliding it to "Сопр...": a CAD ribbon must be readable
    // without hovering, so the width follows the longest label the active language produces.
    QFont font = button->font();
    font.setPointSizeF(std::max(7.5, font.pointSizeF() - 1.0));
    button->setFont(font);
    const int needed = QFontMetrics(font).horizontalAdvance(action->iconText()) + 18;
    button->setMinimumWidth(std::clamp(needed, 54, 132));
    button->setMaximumWidth(std::clamp(needed, 84, 150));
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
    tabs_ = new QTabBar(this);
    tabs_->setObjectName("ribbonTabs");
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    box->addWidget(tabs_);
    auto* stripe = new QFrame(this);
    stripe->setObjectName("ribbonStripe");
    stripe->setFixedHeight(3);
    stripe->setStyleSheet("#ribbonStripe { background: palette(highlight); }");
    pages_ = new QStackedWidget(this);
    pages_->setObjectName("ribbonPages");
    pages_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    box->addWidget(stripe);
    box->addWidget(pages_);
    connect(tabs_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);

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
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addStretch(1);
    // A page keeps its natural width instead of squeezing panel buttons until their captions
    // elide to "От..."; a narrow window scrolls, which is what a real ribbon does too.
    auto* scroll = new QScrollArea(pages_);
    scroll->setObjectName("ribbonScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

void Ribbon::setPageTitles(const QStringList& titles) {
    for (int i = 0; i < tabs_->count() && i < titles.size(); ++i)
        tabs_->setTabText(i, titles[i]);
}

void Ribbon::retranslate(const QStringList& panelTitles) {
    for (int i = 0; i < panelLabels_.size() && i < panelTitles.size(); ++i)
        if (panelLabels_[i])
            panelLabels_[i]->setText(panelTitles[i]);
}

void Ribbon::refreshLayers(const QStringList& names, const QString& current) {
    const QSignalBlocker guard(layer_);
    layer_->clear();
    layer_->addItems(names);
    const int found = current.isEmpty() ? -1 : layer_->findText(current);
    layer_->setCurrentIndex(found >= 0 ? found : (layer_->count() ? 0 : -1));
}
