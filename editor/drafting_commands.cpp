// SPDX-License-Identifier: LGPL-2.1-or-later
#include "edit_controller.hpp"
#include <QProcess>
#include <cmath>
#include <numbers>
namespace {
constexpr double rad = std::numbers::pi / 180;
double numberAt(const QStringList& list, int i, double fallback = 1) {
    if (i >= list.size())
        return fallback;
    bool ok;
    double x = list[i].toDouble(&ok);
    if (!ok || !std::isfinite(x))
        throw std::invalid_argument("invalid numeric parameter");
    return x;
}
} // namespace
std::optional<pd::DimKind> EditController::dimensionKind() const {
    using K = pd::DimKind;
    switch (tool_) {
    case Tool::DimLinear:
        return K::Linear;
    case Tool::DimAligned:
        return K::Aligned;
    case Tool::DimRadius:
        return K::Radius;
    case Tool::DimDiameter:
        return K::Diameter;
    case Tool::DimAngular:
        return K::Angular;
    default:
        return {};
    }
}
bool EditController::finalDimPoint() const {
    return definitions_.size() + 1 == (tool_ == Tool::DimAngular ? 4u : 3u);
}
QString EditController::draftingPrompt() const {
    switch (step_) {
    case Step::LayoutSpec:
        return tr("Layout name, MODEL, or: name A4/A3/A2/A1 L/P 1:N centre [PLAIN]. Sheet options: VIEW, "
                  "ADDVIEW, MARGINS, STAMP.");
    case Step::StyleSpec:
        return tr(
            "ISO-25, ESKD or FILLED/TICK arrowSize textHeight extensionGap overrun textGap precision [prefix "
            "[suffix]]:");
    case Step::Definition:
        if (tool_ == Tool::DimAngular) {
            if (definitions_.empty())
                return tr("Angle vertex:");
            if (!finalDimPoint())
                return tr("Point on ray %1:").arg(definitions_.size());
            return tr("Point on dimension arc:");
        }
        if (tool_ == Tool::DimDiameter || tool_ == Tool::DimRadius) {
            if (definitions_.empty())
                return tr("Circle centre:");
            if (definitions_.size() == 1)
                return tr("Point on circumference:");
            return tr("Leader position:");
        }
        if (definitions_.empty())
            return tool_ == Tool::DimLinear ? tr("First definition point (X or Y for projection):")
                                            : tr("First definition point:");
        return finalDimPoint() ? tr("Position of dimension line:") : tr("Second definition point:");
    case Step::HatchOptions:
        return tr("Pattern SOLID / ANSI31 / GRID / CONCRETE [scale [angle in degrees]]:");
    case Step::BlockName:
        return tr("Name for the new block:");
    case Step::BlockBase:
        return tr("Block base point:");
    case Step::InsertSpec:
        return tr("Block name [X scale [Y scale [rotation in degrees]]]:");
    case Step::Insertion:
        return tr("Insertion point:");
    default:
        return {};
    }
}
pd::edit::Plan EditController::draftPoint(pd::Vec2 p) const {
    pd::edit::Plan plan(doc_);
    std::unique_ptr<pd::Entity> e;
    if (tool_ == Tool::Insert)
        e = std::make_unique<pd::BlockReference>(doc_.block(blockId_), p, factors_, rotation_);
    else {
        auto points = definitions_;
        points.push_back(p);
        e = std::make_unique<pd::Dimension>(*dimensionKind(), std::move(points), style_, axis_);
    }
    e->traits.layer = layer_;
    plan.put(0, std::move(e));
    return plan;
}
std::optional<QString> EditController::draftingAnswer(const QString& s) {
    try {
        if (step_ == Step::StyleSpec)
            return setDimStyle(s);
        if (step_ == Step::Definition && definitions_.empty() && tool_ == Tool::DimLinear &&
            (s.toUpper() == "X" || s.toUpper() == "Y")) {
            axis_ = s.toUpper() == "Y" ? std::numbers::pi / 2 : 0;
            return prompt();
        }
        if (step_ == Step::BlockName) {
            blockName_ = s;
            return transition(Step::BlockBase);
        }
        if (step_ != Step::HatchOptions && step_ != Step::InsertSpec)
            return {};
        auto args = QProcess::splitCommand(s);
        if (args.empty())
            return prompt();
        if (step_ == Step::InsertSpec) {
            if (args.size() > 4)
                return prompt();
            for (auto id : doc_.blockIds())
                if (doc_.block(id)->name() == args[0].toStdString()) {
                    auto sx = numberAt(args, 1), sy = numberAt(args, 2, sx);
                    if (sx == 0 || sy == 0)
                        return tr("Scales must be nonzero.");
                    rotation_ = rad * numberAt(args, 3, 0);
                    factors_ = {sx, sy};
                    blockId_ = id;
                    return transition(Step::Insertion);
                }
            return tr("Unknown block: %1").arg(args[0]);
        }
        static const QStringList patterns{"SOLID", "ANSI31", "GRID", "CONCRETE"};
        int index = patterns.indexOf(args[0].toUpper());
        if (index < 0) {
            QStringList local{tr("SOLID"), tr("ANSI31"), tr("GRID"), tr("CONCRETE")};
            index = local.indexOf(args[0].toUpper());
        }
        if (index < 0 || args.size() > 3)
            return prompt();
        std::vector<pd::Polyline> loops;
        for (auto id : ids_)
            loops.push_back(pd::Hatch::boundary(*doc_.entity(id)));
        pd::edit::Plan plan(doc_);
        auto hatch = std::make_unique<pd::Hatch>(std::move(loops), static_cast<pd::HatchPattern>(index),
                                                 numberAt(args, 1), numberAt(args, 2, 0) * rad);
        hatch->traits.layer = layer_;
        plan.put(0, std::move(hatch));
        return publish(std::move(plan));
    } catch (const std::exception& e) {
        return error(e);
    }
}
QString EditController::setDimStyle(const QString& s) {
    auto args = QProcess::splitCommand(s);
    pd::DimStyle st;
    auto preset = s.toUpper();
    if (preset == "ESKD" || preset == tr("ESKD").toUpper())
        st = pd::DimStyle::eskd();
    else if (preset != "ISO-25") {
        if (args.size() < 7 || args.size() > 9 ||
            !(args[0].toUpper() == "FILLED" || args[0].toUpper() == "TICK"))
            return tr("Use: _DIMSTYLE ISO-25, ESKD or FILLED/TICK arrowSize textHeight extensionGap overrun "
                      "textGap precision [prefix [suffix]]");
        try {
            st.arrow = args[0].toUpper() == "TICK" ? pd::DimStyle::Arrow::Tick : pd::DimStyle::Arrow::Filled;
            st.arrowSize = numberAt(args, 1);
            st.textHeight = numberAt(args, 2);
            st.extensionGap = numberAt(args, 3);
            st.extensionOverrun = numberAt(args, 4);
            st.textGap = numberAt(args, 5);
            bool ok;
            st.precision = args[6].toInt(&ok);
            if (!ok)
                throw std::invalid_argument("invalid precision");
            if (args.size() > 7 && args[7] != "-")
                st.prefix = args[7].toStdString();
            if (args.size() > 8 && args[8] != "-")
                st.suffix = args[8].toStdString();
            st.validate();
        } catch (const std::exception& e) {
            return error(e);
        }
    }
    style_ = std::move(st);
    if (step_ == Step::StyleSpec)
        cancel();
    return tr("Dimension style set.");
}
