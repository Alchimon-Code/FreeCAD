// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"
#include <QProcess>
#include <cmath>
namespace {
double scale(QString s) {
    auto a = s.split(':');
    bool ok = false;
    double n = 0;
    if (a.size() == 2 && a[0] == "1")
        n = a[1].toDouble(&ok);
    if (!ok || !std::isfinite(n) || n <= 0)
        throw std::invalid_argument("expected a scale 1:N");
    return 1 / n;
}
pd::Vec2 point(QString s) {
    auto p = EditController::coordinate(s);
    if (!p)
        throw std::invalid_argument("expected x,y");
    return *p;
}
} // namespace
QStringList CommandSession::layoutNames() const {
    QStringList names;
    for (auto id : document_.layoutIds())
        names << QString::fromStdString(document_.layout(id)->name);
    if (names.empty())
        names << "Layout1";
    return names;
}
std::optional<pd::Layout> CommandSession::activeLayout() const {
    if (space_.isEmpty())
        return {};
    for (auto id : document_.layoutIds())
        if (QString::fromStdString(document_.layout(id)->name) == space_)
            return *document_.layout(id);
    auto box = document_.extents();
    return pd::Layout::standard("Layout1", pd::Paper::A4, true,
                                box.empty() ? pd::Vec2{} : pd::midpoint(box.min(), box.max()), 1);
}
void CommandSession::selectSpace(const QString& s) {
    if (s == space_ || (!s.isEmpty() && !layoutNames().contains(s)))
        return;
    if (editor_.active())
        editor_.cancel();
    space_ = s;
    emit spaceChanged();
}
QString CommandSession::layoutCommand(const QString& tail) {
    auto args = QProcess::splitCommand(tail);
    auto usage = tr("Use: _LAYOUT name A4/A3/A2/A1 L/P 1:N centre [PLAIN], name, MODEL, VIEW/ADDVIEW min max "
                    "centre 1:N, MARGINS left bottom right top, or STAMP designation title");
    try {
        if (args.size() == 1) {
            if (args[0].toUpper() == "MODEL")
                selectSpace({});
            else if (layoutNames().contains(args[0]))
                selectSpace(args[0]);
            else
                return usage;
            if (editor_.active())
                editor_.cancel();
            return tr("Space changed.");
        }
        pd::Layout l;
        if (args.size() == 5 && (args[0].toUpper() == "ADDVIEW" || args[0].toUpper() == "VIEW")) {
            auto current = activeLayout();
            if (!current)
                return usage;
            l = *current;
            if (args[0].toUpper() == "VIEW")
                l.viewports.clear();
            l.viewports.emplace_back(pd::BoundingBox2D(point(args[1]), point(args[2])), point(args[3]),
                                     scale(args[4]));
        } else if (args.size() == 5 && args[0].toUpper() == "MARGINS") {
            auto current = activeLayout();
            if (!current)
                return usage;
            l = *current;
            double m[4];
            for (int i = 0; i < 4; ++i) {
                bool ok;
                m[i] = args[i + 1].toDouble(&ok);
                if (!ok)
                    return usage;
            }
            l.margins = {m[0], m[1], m[2], m[3]};
        } else if (args.size() == 3 && args[0].toUpper() == "STAMP") {
            auto current = activeLayout();
            if (!current)
                return usage;
            l = *current;
            l.frame = l.stamp = true;
            l.designation = args[1].toStdString();
            l.title = args[2].toStdString();
        } else {
            if (args.size() < 5 || args.size() > 6)
                return usage;
            int paper = QStringList{"A4", "A3", "A2", "A1"}.indexOf(args[1].toUpper());
            auto orientation = args[2].toUpper();
            if (paper < 0 || (orientation != "L" && orientation != "P") ||
                (args.size() == 6 && args[5].toUpper() != "PLAIN") || args[0].toUpper() == "MODEL")
                return usage;
            l = pd::Layout::standard(args[0].toStdString(), static_cast<pd::Paper>(paper), orientation == "L",
                                     point(args[4]), scale(args[3]), args.size() == 5);
        }
        pd::EntityId existing = 0;
        for (auto id : document_.layoutIds())
            if (document_.layout(id)->name == l.name)
                existing = id;
        document_.transact([&](pd::Transaction& tx) {
            if (existing)
                tx.updateLayout(existing, l);
            else
                tx.addLayout(l);
        });
        selectSpace(QString::fromStdString(l.name));
        return tr("Layout updated.");
    } catch (const std::exception& e) {
        return tr("Layout rejected: %1").arg(QString::fromUtf8(e.what()));
    }
}
