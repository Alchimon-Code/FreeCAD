// SPDX-License-Identifier: LGPL-2.1-or-later
#include "property_inspector.h"
#include "palette_header.h"
#include "command_session.h"
#include "viewport.h"
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <cmath>
#include <numbers>
namespace {
using namespace pd;
constexpr double rad = std::numbers::pi / 180;
enum Role { Key = Qt::UserRole, Choices, FreeText };
struct Field {
    QString key, label, value;
    QStringList choices;
    bool custom = false;
};
QString num(double n) { return QLocale::c().toString(n, 'g', QLocale::FloatingPointShortest); }
QString xy(Vec2 p) { return num(p.x) + ", " + num(p.y); }
QString angle(Vec2 d) { return num(std::atan2(d.y, d.x) / rad); }
QString str(const std::string& s) { return QString::fromStdString(s); }
QString rgb(Color c) { return QColor(c.r, c.g, c.b).name().toUpper(); }
QString byLayer() { return PropertyInspector::tr("ByLayer"); }
QString byBlock() { return PropertyInspector::tr("ByBlock"); }
template <class T, class F> QString format(const Property<T>& v, F f) {
    if (auto p = std::get_if<T>(&v))
        return f(*p);
    return std::holds_alternative<ByLayer>(v) ? byLayer() : byBlock();
}
QVector<Field> fields(const Entity& e, const Document& d, int vertex, const QStringList& layers) {
    QVector<Field> out;
    auto add = [&](QString k, QString label, QString v, QStringList choices = {}, bool custom = false) {
        out.push_back({k, label, v, choices, custom});
    };
    add("layer", PropertyInspector::tr("Layer"), str(d.layer(e.traits.layer)->name), layers);
    add("color", PropertyInspector::tr("Color · RGB"), format(e.traits.color, rgb),
        {byLayer(), byBlock(), "#FFFFFF", "#E04040", "#3488CC"}, true);
    add("weight", PropertyInspector::tr("Line weight · mm"), format(e.traits.lineWeight, num),
        {byLayer(), byBlock(), "0.18", "0.25", "0.35", "0.5", "0.7"}, true);
    auto centre = [&](Vec2 p) { add("center", PropertyInspector::tr("Centre"), xy(p)); };
    auto rotation = [&](const Matrix3x3& m) {
        add("insertion", PropertyInspector::tr("Insertion point"), xy(m.applyPoint({})));
        add("rotation", PropertyInspector::tr("Rotation · °"), angle(m.applyVector({1, 0})));
    };
    if (auto l = dynamic_cast<const Line*>(&e)) {
        add("start", PropertyInspector::tr("Start point"), xy(l->start()));
        add("end", PropertyInspector::tr("End point"), xy(l->end()));
        add("lineAngle", PropertyInspector::tr("Direction · °"), angle(l->end() - l->start()));
    }
    if (auto c = dynamic_cast<const Circle*>(&e)) {
        centre(c->center());
        add("radius", PropertyInspector::tr("Radius"), num(c->radius()));
    }
    if (auto a = dynamic_cast<const Arc*>(&e)) {
        auto& c = a->curve();
        centre(c.center());
        add("radius", PropertyInspector::tr("Radius"), num(c.radius()));
        add("startAngle", PropertyInspector::tr("Start angle · °"), num(c.startAngle() / rad));
        add("sweep", PropertyInspector::tr("Sweep · °"), num(c.sweep() / rad));
    }
    if (auto p = dynamic_cast<const Polyline*>(&e); p && size_t(vertex) < p->vertices().size()) {
        auto v = p->vertices()[vertex];
        add("vertex", PropertyInspector::tr("Vertex position"), xy(v.point));
        if (p->closed() || size_t(vertex + 1) < p->vertices().size())
            add("bulge", PropertyInspector::tr("Outgoing bulge"), num(v.bulge));
    }
    if (auto t = dynamic_cast<const Text*>(&e)) {
        add("text", PropertyInspector::tr("Text"), str(t->content()));
        rotation(t->placement());
    }
    if (auto b = dynamic_cast<const BlockReference*>(&e)) {
        auto m = b->placement();
        auto x = m.applyVector({1, 0}), y = m.applyVector({0, 1});
        rotation(m);
        add("sx", PropertyInspector::tr("X scale"), num(x.length()));
        add("sy", PropertyInspector::tr("Y scale"), num(std::copysign(y.length(), x.cross(y))));
    }
    if (auto a = dynamic_cast<const Dimension*>(&e)) {
        auto& s = a->style();
        add("style", PropertyInspector::tr("Dimension style"),
            s == DimStyle::iso25()  ? "ISO-25"
            : s == DimStyle::eskd() ? "ESKD"
                                    : PropertyInspector::tr("Custom"),
            {"ISO-25", "ESKD"});
        add("arrow", PropertyInspector::tr("Arrow"),
            s.arrow == DimStyle::Arrow::Filled ? PropertyInspector::tr("Filled")
                                               : PropertyInspector::tr("Tick"),
            {PropertyInspector::tr("Filled"), PropertyInspector::tr("Tick")});
        add("arrowSize", PropertyInspector::tr("Arrow size"), num(s.arrowSize));
        add("height", PropertyInspector::tr("Text height"), num(s.textHeight));
        add("gap", PropertyInspector::tr("Text gap"), num(s.textGap));
        add("extension", PropertyInspector::tr("Extension gap"), num(s.extensionGap));
        add("overrun", PropertyInspector::tr("Extension overrun"), num(s.extensionOverrun));
        add("precision", PropertyInspector::tr("Decimal places"), QString::number(s.precision));
        add("prefix", PropertyInspector::tr("Prefix"), str(s.prefix));
        add("suffix", PropertyInspector::tr("Suffix"), str(s.suffix));
        for (size_t i = 0; i < a->defPoints().size(); ++i)
            add(QString("def%1").arg(i), PropertyInspector::tr("Definition point %1").arg(i + 1),
                xy(a->defPoints()[i]));
    }
    if (auto h = dynamic_cast<const Hatch*>(&e)) {
        add("patternScale", PropertyInspector::tr("Pattern scale"), num(h->scale()));
        add("patternAngle", PropertyInspector::tr("Pattern angle · °"), num(h->angle() / rad));
    }
    return out;
}
std::unique_ptr<Entity> changed(const Document& d, const Entity& e, const QString& k, const QString& v,
                                int vertex) {
    auto invalid = []() { throw std::invalid_argument("invalid property value"); };
    auto n = [&] {
        bool ok = false;
        double x = v.toDouble(&ok);
        if (!ok || !std::isfinite(x))
            invalid();
        return x;
    };
    auto point = [&] {
        auto p = EditController::coordinate(v);
        if (!p)
            invalid();
        return *p;
    };
    auto traits = e.traits;
    auto copy = e.clone();
    if (k == "layer") {
        auto ids = d.layerIds();
        auto it = std::find_if(ids.begin(), ids.end(), [&](auto id) { return str(d.layer(id)->name) == v; });
        if (it == ids.end())
            invalid();
        traits.layer = *it;
    } else if (k == "weight") {
        if (v == byLayer())
            traits.lineWeight = ByLayer{};
        else if (v == byBlock())
            traits.lineWeight = ByBlock{};
        else
            traits.lineWeight = n();
    } else if (k == "color") {
        if (v == byLayer())
            traits.color = ByLayer{};
        else if (v == byBlock())
            traits.color = ByBlock{};
        else {
            QColor c(v);
            if (!c.isValid() || c.alpha() != 255)
                invalid();
            traits.color = Color{(uint8_t)c.red(), (uint8_t)c.green(), (uint8_t)c.blue()};
        }
    } else if (k == "rotation" || k == "insertion") {
        auto t = dynamic_cast<const Text*>(&e);
        auto m = t ? t->placement() : dynamic_cast<const BlockReference&>(e).placement();
        auto p = m.applyPoint({});
        auto x = m.applyVector({1, 0});
        copy->transform(k == "insertion" ? Matrix3x3::translation(point() - p)
                                         : Matrix3x3::translation(p) *
                                               Matrix3x3::rotation(n() * rad - std::atan2(x.y, x.x)) *
                                               Matrix3x3::translation(-p));
    } else if (auto l = dynamic_cast<const Line*>(&e)) {
        auto a = l->start(), b = l->end();
        if (k == "start")
            a = point();
        else if (k == "end")
            b = point();
        else {
            auto t = n() * rad;
            b = a + Vec2{std::cos(t), std::sin(t)} * (b - a).length();
        }
        copy = std::make_unique<Line>(a, b);
    } else if (auto c = dynamic_cast<const Circle*>(&e))
        copy = std::make_unique<Circle>(k == "center" ? point() : c->center(),
                                        k == "radius" ? n() : c->radius());
    else if (auto a = dynamic_cast<const Arc*>(&e)) {
        auto c = a->curve();
        copy = std::make_unique<Arc>(CircularArc(
            k == "center" ? point() : c.center(), k == "radius" ? n() : c.radius(),
            k == "startAngle" ? n() * rad : c.startAngle(), k == "sweep" ? n() * rad : c.sweep()));
    } else if (auto p = dynamic_cast<const Polyline*>(&e)) {
        std::vector<Vertex> vs(p->vertices().begin(), p->vertices().end());
        if (k == "bulge")
            vs.at(vertex).bulge = n();
        else
            vs.at(vertex).point = point();
        copy = std::make_unique<Polyline>(std::move(vs), p->closed());
    } else if (auto t = dynamic_cast<const Text*>(&e)) {
        auto b = t->localInk();
        if (v.isEmpty())
            invalid();
        auto width = (b.max().x - b.min().x) * v.toUcs4().size() /
                     std::max<qsizetype>(1, str(t->content()).toUcs4().size());
        copy = std::make_unique<Text>(v.toStdString(), BoundingBox2D(b.min(), {b.min().x + width, b.max().y}),
                                      t->placement());
    } else if (auto b = dynamic_cast<const BlockReference*>(&e)) {
        auto m = b->placement();
        auto x = m.applyVector({1, 0}), y = m.applyVector({0, 1}), p = m.applyPoint({});
        if (k == "sx")
            x = x * (n() / x.length());
        else
            y = y * (n() / std::copysign(y.length(), x.cross(y)));
        copy = std::make_unique<BlockReference>(d.block(b->definition().id()), Vec2{});
        copy->transform(Matrix3x3(x.x, y.x, p.x, x.y, y.y, p.y));
    } else if (auto a = dynamic_cast<const Dimension*>(&e)) {
        auto s = a->style();
        auto pts = a->defPoints();
        if (k.startsWith("def"))
            pts.at(k.mid(3).toUInt()) = point();
        else if (k == "style") {
            if (v != "ISO-25" && v != "ESKD")
                invalid();
            s = v == "ESKD" ? DimStyle::eskd() : DimStyle::iso25();
        } else if (k == "arrow")
            s.arrow = v == PropertyInspector::tr("Filled") ? DimStyle::Arrow::Filled : DimStyle::Arrow::Tick;
        else if (k == "precision") {
            bool ok;
            s.precision = v.toInt(&ok);
            if (!ok)
                invalid();
        } else if (k == "prefix")
            s.prefix = v.toStdString();
        else if (k == "suffix")
            s.suffix = v.toStdString();
        else if (k == "extension")
            s.extensionGap = n();
        else if (k == "gap")
            s.textGap = n();
        else if (k == "overrun")
            s.extensionOverrun = n();
        else if (k == "height")
            s.textHeight = n();
        else
            s.arrowSize = n();
        copy = std::make_unique<Dimension>(a->kind(), pts, s, a->linearAngle());
    } else if (auto h = dynamic_cast<const Hatch*>(&e))
        copy = std::make_unique<Hatch>(h->loops(), h->pattern(), k == "patternScale" ? n() : h->scale(),
                                       k == "patternAngle" ? n() * rad : h->angle());
    else
        invalid();
    copy->traits = traits;
    return copy;
}
class ValueDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* p, const QStyleOptionViewItem& opt,
                          const QModelIndex& idx) const override {
        auto list = idx.data(Choices).toStringList();
        if (list.empty())
            return QStyledItemDelegate::createEditor(p, opt, idx);
        auto b = new QComboBox(p);
        b->setEditable(idx.data(FreeText).toBool());
        b->addItems(list);
        return b;
    }
    void setEditorData(QWidget* w, const QModelIndex& idx) const override {
        if (auto b = qobject_cast<QComboBox*>(w)) {
            auto v = idx.data().toString();
            b->setCurrentIndex(b->findText(v));
            if (b->isEditable())
                b->setEditText(v);
        } else
            QStyledItemDelegate::setEditorData(w, idx);
    }
    void setModelData(QWidget* w, QAbstractItemModel* m, const QModelIndex& idx) const override {
        if (auto b = qobject_cast<QComboBox*>(w)) {
            if (b->isEditable() || b->currentIndex() >= 0)
                m->setData(idx, b->currentText());
        } else
            QStyledItemDelegate::setModelData(w, m, idx);
    }
};
} // namespace
PropertyInspector::PropertyInspector(QWidget* parent) : QDockWidget(parent) {
    setObjectName("PropertyInspector");
    auto body = new QWidget(this);
    auto layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    summary_ = new QLabel(body);
    layout->addWidget(summary_);
    auto row = new QHBoxLayout;
    vertexLabel_ = new QLabel(body);
    vertex_ = new QSpinBox(body);
    row->addWidget(vertexLabel_);
    row->addWidget(vertex_);
    row->addStretch();
    layout->addLayout(row);
    table_ = new QTableWidget(0, 2, body);
    table_->setObjectName("propertyTable");
    table_->verticalHeader()->hide();
    // AutoCAD's palette packs a property a row; the style's default row is a third taller than the
    // text it carries, which leaves the palette showing fewer properties than it has room for.
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setHorizontalHeader(new PaletteHeader(table_));
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setItemDelegateForColumn(1, new ValueDelegate(table_));
    layout->addWidget(table_);
    setWidget(body);
    connect(table_, &QTableWidget::itemChanged, this, &PropertyInspector::edited);
    connect(vertex_, &QSpinBox::valueChanged, this, [this] { refresh(); });
    retranslate();
}
void PropertyInspector::bind(CommandSession* s, Viewport* v) {
    if (session_)
        disconnect(session_, nullptr, this, nullptr);
    if (view_)
        disconnect(view_, nullptr, this, nullptr);
    session_ = s;
    view_ = v;
    if (s) {
        connect(s, &CommandSession::documentChanged, this, &PropertyInspector::refresh);
        connect(s, &CommandSession::spaceChanged, this, &PropertyInspector::refresh);
    }
    if (v)
        connect(v, &Viewport::selectionChanged, this, &PropertyInspector::refresh);
    refresh();
}
void PropertyInspector::retranslate() {
    setWindowTitle(tr("Properties"));
    vertexLabel_->setText(tr("Vertex"));
    table_->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    refresh();
}
void PropertyInspector::refresh() {
    if (writing_)
        return;
    QSignalBlocker blocker(table_), vb(vertex_);
    table_->setRowCount(0);
    ids_ = view_ && !view_->paperSpace() ? view_->selectedIds() : std::vector<pd::EntityId>{};
    summary_->setText(ids_.empty() ? tr("Select objects in Model") : tr("Selected: %1").arg(ids_.size()));
    vertexLabel_->hide();
    vertex_->hide();
    if (!session_ || ids_.empty())
        return;
    auto& d = session_->document();
    revision_ = d.revision();
    QStringList layers;
    for (auto id : d.layerIds())
        if (!d.layer(id)->isLocked)
            layers << str(d.layer(id)->name);
    bool writable = true;
    int vertices = INT_MAX;
    for (auto id : ids_) {
        auto e = d.entity(id);
        if (!e)
            return;
        writable &= !d.layer(e->traits.layer)->isLocked;
        auto p = dynamic_cast<const pd::Polyline*>(e);
        vertices = p ? std::min(vertices, int(p->vertices().size())) : 0;
    }
    if (vertices) {
        vertex_->setRange(1, vertices);
        vertex_->show();
        vertexLabel_->show();
    }
    int at = vertices ? vertex_->value() - 1 : 0;
    auto data = fields(*d.entity(ids_[0]), d, at, layers);
    for (size_t i = 1; i < ids_.size(); ++i) {
        auto fs = fields(*d.entity(ids_[i]), d, at, layers);
        for (auto it = data.begin(); it != data.end();) {
            auto other = std::find_if(fs.begin(), fs.end(), [&](auto& f) { return f.key == it->key; });
            if (other == fs.end())
                it = data.erase(it);
            else {
                if (it->value != other->value)
                    it->value = tr("*VARIES*");
                ++it;
            }
        }
    }
    for (auto& f : data) {
        int r = table_->rowCount();
        table_->insertRow(r);
        auto label = new QTableWidgetItem(f.label);
        label->setFlags(label->flags() & ~Qt::ItemIsEditable);
        auto val = new QTableWidgetItem(f.value);
        val->setData(Key, f.key);
        val->setData(Choices, f.choices);
        val->setData(FreeText, f.custom);
        if (!writable)
            val->setFlags(val->flags() & ~Qt::ItemIsEditable);
        table_->setItem(r, 0, label);
        table_->setItem(r, 1, val);
    }
    if (!writable)
        summary_->setText(tr("Selection includes a locked layer"));
}
void PropertyInspector::edited(QTableWidgetItem* item) {
    if (writing_ || !session_ || !view_ || item->column() != 1)
        return;
    auto& d = session_->document();
    if (d.revision() != revision_ || view_->selectedIds() != ids_) {
        refresh();
        return;
    }
    auto key = item->data(Key).toString(), v = item->text();
    auto ids = ids_;
    {
        QScopedValueRollback guard(writing_, true);
        try {
            d.transact([&](pd::Transaction& tx) {
                for (auto id : ids) {
                    auto p = changed(d, *tx.entity(id), key, v, vertex_->value() - 1);
                    tx.replace(id, *p);
                }
            });
        } catch (const std::exception&) {
            emit message(tr("Property not changed. Check the value and layer lock."));
        }
    }
    QTimer::singleShot(0, this, &PropertyInspector::refresh);
}
