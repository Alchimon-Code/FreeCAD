// SPDX-License-Identifier: LGPL-2.1-or-later
#include "layer_manager.h"
#include "palette_header.h"
#include "command_session.h"
#include <QColorDialog>
#include <QHeaderView>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <set>
namespace {
enum Column { Current, Name, Visible, Frozen, Locked, Color, Weight, Count };
}
LayerManager::LayerManager(QWidget* parent) : QDockWidget(parent) {
    setObjectName("LayerManager");
    auto body = new QWidget(this);
    auto layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    table_ = new QTableWidget(0, Count, body);
    table_->setObjectName("layerTable");
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setHorizontalHeader(new PaletteHeader(table_));
    // AutoCAD's layer palette is narrower than its columns want to be: the name takes the slack and
    // every other column is stated, so the palette stops squeezing the drawing area beside it.
    auto* header = table_->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setHighlightSections(false);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(24);
    header->setSectionResizeMode(Name, QHeaderView::Stretch);
    header->resizeSection(Current, 78);
    header->resizeSection(Visible, 50);
    header->resizeSection(Frozen, 62);
    header->resizeSection(Locked, 56);
    header->resizeSection(Color, 92);
    header->resizeSection(Weight, 68);
    layout->addWidget(table_);
    auto buttons = new QHBoxLayout;
    add_ = new QPushButton(body);
    remove_ = new QPushButton(body);
    add_->setObjectName("layerAdd");
    remove_->setObjectName("layerRemove");
    buttons->addWidget(add_);
    buttons->addWidget(remove_);
    buttons->addStretch();
    layout->addLayout(buttons);
    setWidget(body);
    connect(add_, &QPushButton::clicked, this, &LayerManager::createLayer);
    connect(remove_, &QPushButton::clicked, this, &LayerManager::removeLayer);
    connect(table_, &QTableWidget::itemChanged, this, &LayerManager::edited);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int r, int c) {
        if (c == Color)
            pickColor(r);
    });
    retranslate();
}
void LayerManager::bind(CommandSession* s) {
    if (session_) {
        disconnect(session_, nullptr, this, nullptr);
        disconnect(&session_->editor(), nullptr, this, nullptr);
    }
    session_ = s;
    if (s) {
        connect(&s->editor(), &EditController::activeLayerChanged, this, &LayerManager::refresh);
        connect(s, &CommandSession::documentChanged, this, [this](const QList<pd::Change>& batch) {
            if (std::any_of(batch.begin(), batch.end(), [](const auto& c) {
                    return c.kind == pd::Change::Kind::Reset || c.object == pd::Change::Object::Layer;
                }))
                refresh();
        });
    }
    refresh();
}
void LayerManager::retranslate() {
    setWindowTitle(tr("Layers"));
    table_->setHorizontalHeaderLabels({tr("Current"), tr("Name"), tr("Visible"), tr("Frozen"), tr("Locked"),
                                       tr("Color"), tr("Weight · mm")});
    add_->setText(tr("New layer"));
    remove_->setText(tr("Delete layer"));
    table_->setToolTip(
        tr("Check Current to draw on a layer. Double-click a name, color or weight to edit it."));
    refresh();
}
void LayerManager::refresh() {
    if (writing_)
        return;
    QSignalBlocker b(table_);
    auto cur = table_->currentItem();
    auto selected = cur ? cur->data(Qt::UserRole).toULongLong() : 0;
    table_->setRowCount(0);
    if (!session_)
        return;
    auto& d = session_->document();
    for (auto id : d.layerIds()) {
        auto& l = *d.layer(id);
        auto row = table_->rowCount();
        table_->insertRow(row);
        for (int c = 0; c < Count; ++c) {
            auto item = new QTableWidgetItem;
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
            auto flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
            switch (c) {
            case Name:
                item->setText(QString::fromStdString(l.name));
                if (id != pd::DefaultLayer)
                    flags |= Qt::ItemIsEditable;
                break;
            case Color: {
                auto color = QColor(l.color.r, l.color.g, l.color.b);
                item->setText(color.name().toUpper());
                item->setData(Qt::DecorationRole, color);
                break;
            }
            case Weight:
                item->setText(QString::number(l.lineWeight, 'g', 15));
                flags |= Qt::ItemIsEditable;
                break;
            default:
                flags |= Qt::ItemIsUserCheckable;
                item->setCheckState((c == Current   ? session_->editor().activeLayer() == id
                                     : c == Visible ? l.isVisible
                                     : c == Frozen  ? l.isFrozen
                                                    : l.isLocked)
                                        ? Qt::Checked
                                        : Qt::Unchecked);
            }
            item->setFlags(flags);
            table_->setItem(row, c, item);
        }
        if (id == selected)
            table_->selectRow(row);
    }
}
void LayerManager::edited(QTableWidgetItem* i) {
    if (!session_ || writing_)
        return;
    auto id = i->data(Qt::UserRole).toULongLong();
    auto col = i->column();
    auto on = i->checkState() == Qt::Checked;
    {
        QScopedValueRollback guard(writing_, true);
        try {
            if (col == Current) {
                if (on)
                    session_->editor().setActiveLayer(id);
            } else
                session_->document().transact([&](pd::Transaction& t) {
                    if (!t.layer(id))
                        throw std::invalid_argument("missing layer");
                    auto l = *t.layer(id);
                    switch (col) {
                    case Name:
                        l.name = i->text().trimmed().toStdString();
                        break;
                    case Visible:
                        l.isVisible = on;
                        break;
                    case Frozen:
                        l.isFrozen = on;
                        break;
                    case Locked:
                        l.isLocked = on;
                        break;
                    case Color: {
                        QColor c(i->text());
                        if (!c.isValid())
                            throw std::invalid_argument("invalid color");
                        l.color = {(uint8_t)c.red(), (uint8_t)c.green(), (uint8_t)c.blue()};
                        break;
                    }
                    case Weight: {
                        bool ok;
                        l.lineWeight = i->text().toDouble(&ok);
                        if (!ok)
                            throw std::invalid_argument("invalid weight");
                        break;
                    }
                    default:
                        return;
                    }
                    t.updateLayer(id, l);
                });
        } catch (const std::exception&) {
            emit message(tr("Layer not changed. Check the name and values."));
        }
    }
    QTimer::singleShot(0, this, &LayerManager::refresh);
}
void LayerManager::pickColor(int row) {
    auto item = table_->item(row, Color);
    auto id = item->data(Qt::UserRole);
    auto owner = session_;
    auto color = QColorDialog::getColor(QColor(item->text()), this, tr("Layer color"),
                                        QColorDialog::DontUseNativeDialog);
    if (!color.isValid() || owner != session_)
        return;
    for (int r = 0; r < table_->rowCount(); ++r)
        if (table_->item(r, Color)->data(Qt::UserRole) == id) {
            table_->item(r, Color)->setText(color.name().toUpper());
            break;
        }
}
void LayerManager::createLayer() {
    if (!session_)
        return;
    auto& d = session_->document();
    std::set<std::string> used;
    for (auto id : d.layerIds())
        used.insert(d.layer(id)->name);
    int i = 1;
    std::string name;
    do {
        name = tr("Layer %1").arg(i++).toStdString();
    } while (used.contains(name));
    pd::LayerId id;
    try {
        d.transact([&](pd::Transaction& t) { id = t.addLayer(pd::Layer(name)); });
    } catch (const std::exception&) {
        emit message(tr("Cannot create layer."));
        return;
    }
    refresh();
    for (int r = 0; r < table_->rowCount(); ++r) {
        auto item = table_->item(r, Name);
        if (item->data(Qt::UserRole).toULongLong() == id) {
            table_->setCurrentItem(item);
            table_->editItem(item);
            break;
        }
    }
}
void LayerManager::removeLayer() {
    if (!session_ || !table_->currentItem())
        return;
    auto id = table_->currentItem()->data(Qt::UserRole).toULongLong();
    try {
        session_->document().transact([&](pd::Transaction& t) { t.removeLayer(id); });
    } catch (const std::exception&) {
        emit message(tr("Layer 0 and layers used by drawing objects cannot be deleted."));
    }
}
