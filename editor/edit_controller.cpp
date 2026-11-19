// SPDX-License-Identifier: LGPL-2.1-or-later
#include "edit_controller.hpp"
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <planedraft/drafting.hpp>
namespace {
using Tool = EditController::Tool;
using Step = EditController::Step;
constexpr double rad = std::numbers::pi / 180;
std::optional<pd::EntityId> handle(QString s) {
    if (s.startsWith('#'))
        s.remove(0, 1);
    bool ok;
    auto id = s.toULongLong(&ok);
    if (!ok || !id)
        return {};
    return id;
}
} // namespace
std::optional<pd::Vec2> EditController::coordinate(const QString& s) {
    auto fields = s.split(',');
    if (fields.size() != 2)
        return {};
    bool x, y;
    pd::Vec2 p{fields[0].toDouble(&x), fields[1].toDouble(&y)};
    return x && y && p.finite() ? std::optional{p} : std::nullopt;
}
pd::edit::Tracked EditController::track(pd::Vec2 base, pd::Vec2 candidate) const {
    return pd::edit::constrain(base, candidate, mode_, degrees_ * rad);
}
std::span<const EditController::NamedTool> EditController::vocabulary() {
    static constexpr NamedTool names[] = {
        {QT_TRANSLATE_NOOP("CommandNames", "LINE"), Tool::Line},
        {QT_TRANSLATE_NOOP("CommandNames", "MOVE"), Tool::Move},
        {QT_TRANSLATE_NOOP("CommandNames", "COPY"), Tool::Copy},
        {QT_TRANSLATE_NOOP("CommandNames", "ROTATE"), Tool::Rotate},
        {QT_TRANSLATE_NOOP("CommandNames", "SCALE"), Tool::Scale},
        {QT_TRANSLATE_NOOP("CommandNames", "MIRROR"), Tool::Mirror},
        {QT_TRANSLATE_NOOP("CommandNames", "TRIM"), Tool::Trim},
        {QT_TRANSLATE_NOOP("CommandNames", "EXTEND"), Tool::Extend},
        {QT_TRANSLATE_NOOP("CommandNames", "OFFSET"), Tool::Offset},
        {QT_TRANSLATE_NOOP("CommandNames", "FILLET"), Tool::Fillet},
        {QT_TRANSLATE_NOOP("CommandNames", "ERASE"), Tool::Erase},
        {QT_TRANSLATE_NOOP("CommandNames", "DIMLINEAR"), Tool::DimLinear},
        {QT_TRANSLATE_NOOP("CommandNames", "DIMALIGNED"), Tool::DimAligned},
        {QT_TRANSLATE_NOOP("CommandNames", "DIMRADIUS"), Tool::DimRadius},
        {QT_TRANSLATE_NOOP("CommandNames", "DIMDIAMETER"), Tool::DimDiameter},
        {QT_TRANSLATE_NOOP("CommandNames", "DIMANGULAR"), Tool::DimAngular},
        {QT_TRANSLATE_NOOP("CommandNames", "HATCH"), Tool::Hatch},
        {QT_TRANSLATE_NOOP("CommandNames", "BLOCK"), Tool::Block},
        {QT_TRANSLATE_NOOP("CommandNames", "INSERT"), Tool::Insert},
        {QT_TRANSLATE_NOOP("CommandNames", "EXPLODE"), Tool::Explode},
        {QT_TRANSLATE_NOOP("CommandNames", "LAYOUT"), Tool::Layout},
        {QT_TRANSLATE_NOOP("CommandNames", "DIMSTYLE"), Tool::DimStyle}};
    return names;
}
std::optional<Tool> EditController::named(const QString& name) {
    for (auto [s, t] : vocabulary())
        if (name == QLatin1String(s))
            return t;
    return {};
}
QString EditController::prompt() const {
    switch (step_) {
    case Step::Idle:
        return {};
    case Step::Select:
        if (tool_ == Tool::Trim || tool_ == Tool::Extend)
            return tr("Select boundary edges, then Enter (or type #IDs).");
        return tr("Select objects, then Enter (or type #IDs).");
    case Step::Base:
        return tool_ == Tool::Line     ? tr("First point:")
               : tool_ == Tool::Mirror ? tr("First point of mirror axis:")
                                       : tr("Base point:");
    case Step::Destination:
        return tool_ == Tool::Line ? tr("End point:") : tr("Destination point:");
    case Step::Angle:
        return tr("Rotation angle in degrees or direction from base (+X = 0):");
    case Step::Factor:
        return tr("Factor or size point (reference %1); R for reference, XY sx sy for axis scales:")
            .arg(reference_, 0, 'g', 8);
    case Step::Reference:
        return tr("Reference length or reference point:");
    case Step::Axis:
        return tr("Second point of mirror axis:");
    case Step::Target:
        return tool_ == Tool::Trim ? tr("Pick the part to trim (or type #ID x,y):")
                                   : tr("Pick the end to extend (or type #ID x,y):");
    case Step::Distance:
        return tr("Offset distance (> 0):");
    case Step::Side:
        return tr("Point on side for offset:");
    case Step::Radius:
        return tr("Fillet radius (0 for sharp corner):");
    case Step::FirstLine:
        return tr("Pick the retained side of the first line (or #ID x,y):");
    case Step::SecondLine:
        return tr("Pick the retained side of the second line (or #ID x,y):");
    default:
        return draftingPrompt();
    case Step::Hot:
        return tr("Grip destination: click, drag and release, or enter coordinates.");
    }
    return {};
}
QString EditController::transition(Step s) {
    step_ = s;
    cursor_.reset();
    hit_.reset();
    ghost_.reset();
    guided_ = false;
    emit stateChanged();
    return prompt();
}
QString EditController::cancel() {
    tool_ = Tool::None;
    ids_.clear();
    base_.reset();
    first_ = 0;
    transition(Step::Idle);
    return tr("Cancelled.");
}
QString EditController::error(const std::exception& e) const {
    if (dynamic_cast<const pd::NotSimilarity*>(&e))
        return tr("Circles, arcs and bulge segments require uniform scaling.");
    return tr("Edit rejected: %1").arg(QString::fromUtf8(e.what()));
}
QString EditController::start(Tool tool, Ids ids) {
    if (active())
        cancel();
    tool_ = tool;
    if (tool == Tool::Layout)
        return transition(Step::LayoutSpec);
    if (tool == Tool::DimStyle)
        return transition(Step::StyleSpec);
    if (dimensionKind()) {
        definitions_.clear();
        axis_ = 0;
        if ((tool == Tool::DimRadius || tool == Tool::DimDiameter) && ids.size() == 1) {
            if (auto c = dynamic_cast<const pd::Circle*>(doc_.entity(ids.front())))
                definitions_ = {c->center(), c->center() + pd::Vec2{c->radius(), 0}};
        }
        return transition(Step::Definition);
    }
    if (tool == Tool::Insert)
        return transition(Step::InsertSpec);
    if (tool == Tool::Line)
        return transition(Step::Base);
    transition(Step::Select);
    return ids.empty() ? prompt() : selectionDone(std::move(ids));
}
QString EditController::selectionDone(Ids ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.empty())
        return prompt();
    for (auto id : ids) {
        auto e = doc_.entity(id);
        if (!e || !doc_.visible(*e))
            return tr("Select visible drawing entities.");
        if (tool_ != Tool::Trim && tool_ != Tool::Extend && tool_ != Tool::Hatch &&
            doc_.layer(e->traits.layer)->isLocked)
            return tr("Selection contains a locked layer.");
        if ((tool_ == Tool::Trim || tool_ == Tool::Extend || tool_ == Tool::Offset) &&
            !(dynamic_cast<const pd::Line*>(e) || dynamic_cast<const pd::Arc*>(e) ||
              dynamic_cast<const pd::Circle*>(e) || dynamic_cast<const pd::Polyline*>(e)))
            return tr("Select lines, circles, arcs or polylines.");
        if (tool_ == Tool::Hatch && !dynamic_cast<const pd::Circle*>(e)) {
            auto p = dynamic_cast<const pd::Polyline*>(e);
            if (!p || !p->closed())
                return tr("Select circles or closed polylines, including the holes.");
        }
        if (tool_ == Tool::Fillet && !dynamic_cast<const pd::Line*>(e))
            return tr("Select one or two lines.");
    }
    if (tool_ == Tool::Offset && ids.size() != 1)
        return tr("Select exactly one object for offset.");
    if (tool_ == Tool::Fillet && ids.size() > 2)
        return tr("Select one or two lines.");
    ids_ = std::move(ids);
    switch (tool_) {
    case Tool::Trim:
    case Tool::Extend:
        return transition(Step::Target);
    case Tool::Fillet:
        return transition(Step::Radius);
    case Tool::Offset:
        return transition(Step::Distance);
    case Tool::Hatch:
        return transition(Step::HatchOptions);
    case Tool::Block:
        return transition(Step::BlockName);
    case Tool::Explode:
        try {
            return publish(pd::explode(doc_, ids_));
        } catch (const std::exception& e) {
            return error(e);
        }
    case Tool::Erase:
        try {
            return publish(pd::edit::erase(doc_, ids_));
        } catch (const std::exception& e) {
            return error(e);
        }
    default:
        return transition(Step::Base);
    }
}
QString EditController::beginGrip(pd::EntityId id, size_t i) {
    if (active())
        return prompt();
    auto e = doc_.entity(id);
    if (!e || doc_.layer(e->traits.layer)->isLocked)
        return tr("Grip is not editable.");
    auto pts = e->getGripPoints();
    if (i >= pts.size())
        return tr("Grip is not editable.");
    tool_ = Tool::Grip;
    first_ = id;
    gripIndex_ = i;
    base_ = pts[i];
    return transition(Step::Hot);
}
void EditController::setTracking(pd::edit::Tracking m) {
    if (mode_ != m) {
        mode_ = m;
        emit stateChanged();
    }
}
void EditController::setPolarDegrees(int d) {
    if ((d == 15 || d == 30 || d == 45) && degrees_ != d) {
        degrees_ = d;
        emit stateChanged();
    }
}
pd::edit::Plan EditController::proposal(pd::Vec2 p) const {
    using namespace pd::edit;
    if (dimensionKind() || tool_ == Tool::Insert)
        return draftPoint(p);
    switch (tool_) {
    case Tool::Line:
        return line(doc_, *base_, p, layer_);
    case Tool::Move:
        return move(doc_, ids_, p - *base_);
    case Tool::Copy:
        return copy(doc_, ids_, p - *base_);
    case Tool::Rotate:
        return rotateToward(doc_, ids_, *base_, p);
    case Tool::Scale:
        return scaleTo(doc_, ids_, *base_, reference_, p);
    case Tool::Mirror:
        return mirror(doc_, ids_, *base_, p);
    case Tool::Grip:
        return grip(doc_, first_, gripIndex_, p);
    case Tool::Offset:
        return offset(doc_, ids_.front(), value_, p);
    default:
        throw std::logic_error("No point proposal for this step.");
    }
}
void EditController::hover(std::optional<pd::Vec2> p, std::optional<pd::EntityId> hit) {
    bool guide = false;
    if (p && active() && base_) {
        try {
            auto t = track(*base_, *p);
            p = t.point;
            guide = t.guided;
        } catch (const std::invalid_argument&) {
            p.reset();
        }
    }
    if (p == cursor_ && hit == hit_ && guide == guided_)
        return;
    cursor_ = p;
    hit_ = hit;
    guided_ = guide;
    ghost_.reset();
    if (!p || !active())
        return;
    switch (step_) {
    case Step::Definition:
        if (!finalDimPoint())
            break;
        [[fallthrough]];
    case Step::Insertion:
    case Step::Destination:
    case Step::Angle:
    case Step::Factor:
    case Step::Axis:
    case Step::Hot:
    case Step::Side:
        try {
            ghost_.emplace(proposal(*cursor_));
        } catch (const std::exception&) { /* invalid intermediate shape; commit reports why */
        }
        break;
    default:
        break;
    }
}
QString EditController::publish(pd::edit::Plan plan) {
    plan.apply(doc_);
    if (active())
        cancel();
    return tr("Edit completed.");
}
QString EditController::pick(pd::Vec2 p, std::optional<pd::EntityId> hit) {
    if (!p.finite())
        return tr("Invalid point.");
    try {
        if (base_ && cursor_ != p)
            p = track(*base_, p).point;
        switch (step_) {
        case Step::Base:
            base_ = p;
            if (tool_ == Tool::Scale) {
                reference_ = pd::edit::referenceSize(doc_, ids_, p);
                return transition(Step::Factor);
            }
            return transition(tool_ == Tool::Rotate   ? Step::Angle
                              : tool_ == Tool::Mirror ? Step::Axis
                                                      : Step::Destination);
        case Step::Reference:
            return number((p - *base_).length());
        case Step::Target:
            if (!hit)
                return prompt();
            return publish(tool_ == Tool::Trim ? pd::edit::trim(doc_, *hit, ids_, p)
                                               : pd::edit::extend(doc_, *hit, ids_, p));
        case Step::FirstLine:
        case Step::SecondLine: {
            if (!hit || !dynamic_cast<const pd::Line*>(doc_.entity(*hit)))
                return tr("Pick a line.");
            if ((step_ == Step::FirstLine || ids_.size() == 2) &&
                !std::binary_search(ids_.begin(), ids_.end(), *hit))
                return tr("Pick a selected line.");
            if (step_ == Step::FirstLine) {
                first_ = *hit;
                firstPick_ = p;
                return transition(Step::SecondLine);
            }
            return publish(pd::edit::fillet(doc_, first_, firstPick_, *hit, p, value_));
        }
        case Step::BlockBase: {
            auto id = pd::makeBlock(doc_, blockName_.toStdString(), p, ids_, layer_);
            return tr("Block defined: #%1.").arg(id);
        }
        case Step::Definition:
            if (!finalDimPoint()) {
                if (!definitions_.empty() && (p - definitions_.front()).length() <= pd::EPSILON)
                    return tr("Use distinct definition points.");
                definitions_.push_back(p);
                base_ = p;
                return transition(Step::Definition);
            }
            [[fallthrough]];
        case Step::Insertion:
        case Step::Destination:
        case Step::Angle:
        case Step::Factor:
        case Step::Axis:
        case Step::Hot:
        case Step::Side:
            if (ghost_ && cursor_ == p) {
                auto plan = std::move(*ghost_);
                ghost_.reset();
                return publish(std::move(plan));
            }
            return publish(proposal(p));
        default:
            return prompt();
        }
    } catch (const std::exception& e) {
        return error(e);
    }
}
QString EditController::number(double n) {
    if (!std::isfinite(n))
        return tr("Enter a finite number.");
    try {
        switch (step_) {
        case Step::Distance:
            if (n <= 0)
                return prompt();
            value_ = n;
            return transition(Step::Side);
        case Step::Radius:
            if (n < 0)
                return prompt();
            value_ = n;
            return transition(Step::FirstLine);
        case Step::Angle:
            return publish(pd::edit::rotate(doc_, ids_, *base_, n * rad));
        case Step::Factor:
            return publish(pd::edit::scale(doc_, ids_, *base_, n, n));
        case Step::Reference:
            if (n <= pd::EPSILON)
                return prompt();
            reference_ = n;
            return transition(Step::Factor);
        default:
            return prompt();
        }
    } catch (const std::exception& e) {
        return error(e);
    }
}
QString EditController::enter(Ids ids) {
    if (step_ == Step::Select)
        return selectionDone(std::move(ids));
    return cursor_ ? pick(*cursor_, hit_) : prompt();
}
std::optional<QString> EditController::tryAnswer(const QString& text, Ids selection) {
    auto s = text.simplified();
    if (s.isEmpty())
        return enter(std::move(selection));
    if (!s.startsWith('_'))
        if (auto reply = draftingAnswer(s))
            return reply;
    auto words = s.split(' ');
    if (step_ == Step::Select) {
        Ids typed;
        for (auto& w : words) {
            auto id = handle(w);
            if (!id)
                return {};
            typed.push_back(*id);
        }
        return selectionDone(std::move(typed));
    }
    if (step_ == Step::Factor && words[0].compare("XY", Qt::CaseInsensitive) == 0) {
        if (words.size() != 3)
            return prompt();
        bool okX, okY;
        double x = words[1].toDouble(&okX), y = words[2].toDouble(&okY);
        if (!okX || !okY || !std::isfinite(x) || !std::isfinite(y))
            return tr("Enter a finite number.");
        try {
            return publish(pd::edit::scale(doc_, ids_, *base_, x, y));
        } catch (const std::exception& err) {
            return error(err);
        }
    }
    if (step_ == Step::Factor && s.compare("R", Qt::CaseInsensitive) == 0)
        return transition(Step::Reference);
    if (pickingEntity() && words.size() == 2) {
        auto id = handle(words.front());
        auto p = coordinate(words.back());
        if (id)
            return p ? pick(*p, *id) : prompt();
    }
    if (auto p = coordinate(s))
        return pick(*p);
    bool ok;
    double n = s.toDouble(&ok);
    if (ok)
        return number(n);
    return {};
}
