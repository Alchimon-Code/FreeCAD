// SPDX-License-Identifier: LGPL-2.1-or-later
#include "command_session.h"

#include <algorithm>
#include <planedraft/entities/primitives.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSettings>
#include <QTranslator>
#include <stdexcept>

namespace {
const std::vector<const char*>& builtins() {
    static const auto names = []() {
        std::vector<const char*> v{
            QT_TRANSLATE_NOOP("CommandNames", "UNDO"),      QT_TRANSLATE_NOOP("CommandNames", "REDO"),
            QT_TRANSLATE_NOOP("CommandNames", "RUNSCRIPT"), QT_TRANSLATE_NOOP("CommandNames", "OPEN"),
            QT_TRANSLATE_NOOP("CommandNames", "SAVE"),      QT_TRANSLATE_NOOP("CommandNames", "SAVEAS"),
            QT_TRANSLATE_NOOP("CommandNames", "EXPORTSVG"), QT_TRANSLATE_NOOP("CommandNames", "EXPORTPDF"),
            QT_TRANSLATE_NOOP("CommandNames", "ZOOM"),       QT_TRANSLATE_NOOP("CommandNames", "COPYCLIP"),
            QT_TRANSLATE_NOOP("CommandNames", "CUTCLIP"),    QT_TRANSLATE_NOOP("CommandNames", "PASTECLIP"),
            QT_TRANSLATE_NOOP("CommandNames", "COPYBASE")};
        for (auto entry : EditController::vocabulary())
            v.push_back(entry.global);
        return v;
    }();
    return names;
}
QString localName(const char* name) { return QCoreApplication::translate("CommandNames", name); }

bool russianNames() { return localName(QT_TRANSLATE_NOOP("CommandNames", "en")) == QStringLiteral("ru"); }
std::set<std::string> reservedNames() {
    std::set<std::string> names;
    QTranslator ru;
    const bool loaded = ru.load(":/i18n/planedraft_ru.qm");
    for (auto name : builtins()) {
        names.insert(name);
        if (loaded)
            names.insert(ru.translate("CommandNames", name).toStdString());
    }
    return names;
}
std::filesystem::path filePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}
} // namespace

QString CommandSession::resolve(QString token) const {
    token = token.toUpper();
    const bool global = token.startsWith('_');
    if (global) {
        token.remove(0, 1);
    }
    for (const auto* name : builtins()) {
        if (token == QLatin1String(name) || (!global && token == localName(name).toUpper())) {
            return QString::fromLatin1(name);
        }
    }
    auto ext = registry_.resolve(token.toStdString(), !global && russianNames());
    if (!ext.empty())
        return QString::fromStdString(ext);
    // Resolve targets lazily: aliases.ini may be loaded before a DLL/script, or survive its unload.
    const auto it = aliases_.constFind(token);
    return !global && it != aliases_.cend() ? resolve("_" + it.value()) : QString{};
}

QStringList CommandSession::completions() const {
    QStringList names;
    for (const auto* name : builtins()) {
        names << localName(name) << "_" + QString::fromLatin1(name);
    }
    for (const auto& [key, cmd] : registry_.entries()) {
        auto name = QString::fromStdString(key);
        names << name << "_" + name;
        if (russianNames() && !cmd.ru.empty())
            names << QString::fromStdString(cmd.ru);
    }
    for (auto it = aliases_.cbegin(); it != aliases_.cend(); ++it)
        if (names.contains("_" + it.value()))
            names << it.key();
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QHash<QString, QString> CommandSession::defaultAliases() {
    // The classic acad.pgp set, plus the Russian keyboard names requested for the RU interface.
    // Aliases are matched exactly like command names: typing the alias at the prompt runs the
    // global command, so the table works in both UI languages.
    return {
        {"L", "LINE"},         {"ОТ", "LINE"},          {"PL", "PLINE"},       {"ПЛ", "PLINE"},
        {"C", "CIRCLE"},       {"К", "CIRCLE"},         {"A", "ARC"},          {"Д", "ARC"},
        {"REC", "RECTANG"},    {"ПРЯМ", "RECTANG"},     {"POL", "POLYGON"},    {"МН", "POLYGON"},
        {"M", "MOVE"},         {"П", "MOVE"},           {"CO", "COPY"},        {"CP", "COPY"},
        {"КОП", "COPY"},       {"RO", "ROTATE"},        {"ПОВ", "ROTATE"},     {"SC", "SCALE"},
        {"М", "SCALE"},        {"MI", "MIRROR"},        {"ЗЕР", "MIRROR"},     {"TR", "TRIM"},
        {"ОБР", "TRIM"},       {"EX", "EXTEND"},        {"УДЛ", "EXTEND"},     {"O", "OFFSET"},
        {"СМ", "OFFSET"},      {"F", "FILLET"},         {"СОПР", "FILLET"},    {"CHA", "CHAMFER"},
        {"ФАС", "CHAMFER"},    {"E", "ERASE"},          {"СТ", "ERASE"},       {"H", "HATCH"},
        {"Ш", "HATCH"},        {"B", "BLOCK"},          {"Б", "BLOCK"},        {"I", "INSERT"},
        {"ВСТ", "INSERT"},     {"X", "EXPLODE"},        {"РАСЧ", "EXPLODE"},   {"Z", "ZOOM"},
        {"З", "ZOOM"},         {"DI", "DIST"},          {"ДИСТ", "DIST"},      {"AA", "AREA"},
        {"ПЛОЩАДЬ", "AREA"},   {"AR", "ARRAY"},         {"МАССИВ", "ARRAY"},   {"MA", "MATCHPROP"},
        {"КОПИРОВАТЬСВ", "MATCHPROP"},
        // The rest of the acad.pgp keys whose commands exist here: one-step undo, paste, zoom
        // extents, the dimension style and the dimension shorthands.
        {"U", "UNDO"},          {"PA", "PASTECLIP"},      {"ZE", "ZOOM"},         {"SE", "DIMSTYLE"},
        {"DL", "DIMLINEAR"},    {"DLI", "DIMLINEAR"},     {"DAL", "DIMALIGNED"},  {"DRA", "DIMRADIUS"},
        {"DDI", "DIMDIAMETER"}, {"DAN", "DIMANGULAR"}};
}

void CommandSession::loadAliases(const QString& iniPath) {
    QSettings ini(iniPath, QSettings::IniFormat);
    ini.beginGroup("aliases");
    aliases_ = defaultAliases();
    for (const auto& key : ini.childKeys()) {
        static const QRegularExpression validKey(QStringLiteral("\\A[\\p{L}][\\p{L}\\p{N}_]*\\z"));
        static const QRegularExpression globalName(QStringLiteral("\\A[A-Z][A-Z0-9_]{0,31}\\z"));
        auto target = ini.value(key).toString().trimmed().toUpper();
        if (target.startsWith('_'))
            target.remove(0, 1);
        if (validKey.match(key).hasMatch() && globalName.match(target).hasMatch()) {
            aliases_.insert(key.toUpper(), target);
        }
    }
    emit vocabularyChanged();
}

QString CommandSession::loadPlugin(const QString& absolutePath) {
    if (executing_)
        return tr("Another command is active.");
    QScopedValueRollback guard(executing_, true);
    try {
        plugins_.load(filePath(absolutePath));
        emit vocabularyChanged();
        return {};
    } catch (const std::exception& e) {
        return tr("Plugin error: %1").arg(QString::fromUtf8(e.what()));
    }
}
QString CommandSession::unloadPlugin(const QString& absolutePath) {
    if (executing_)
        return tr("Another command is active.");
    QScopedValueRollback guard(executing_, true);
    try {
        plugins_.unload(filePath(absolutePath));
        emit vocabularyChanged();
        return {};
    } catch (const std::exception& e) {
        return tr("Plugin error: %1").arg(QString::fromUtf8(e.what()));
    }
}
void CommandSession::loadPluginDirectory(const QString& path) {
    if (executing_) {
        emit consoleOutput(true, tr("Another command is active."));
        return;
    }
    QScopedValueRollback guard(executing_, true);
    try {
        plugins_.loadDirectory(filePath(path), context().output);
        emit vocabularyChanged();
    } catch (const std::exception& e) {
        emit consoleOutput(true, tr("Plugin error: %1").arg(QString::fromUtf8(e.what())));
    }
}
pd::ExtensionContext CommandSession::context() {
    return {document_, [this] { return selection_ ? selection_() : std::vector<pd::EntityId>{}; },
            [this](bool err, std::string_view s) {
                emit consoleOutput(err, QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())));
            }};
}
QString CommandSession::fileCommand(const QString& name, QString path) {
    const bool open = name == "OPEN", svg = name == "EXPORTSVG", pdf = name == "EXPORTPDF",
               drawing = !svg && !pdf;
    if (path.isEmpty() && name == "SAVE")
        path = currentFile_;
    if (path.isEmpty()) {
        if (!picker_)
            return tr("A file path is required.");
        path = picker_(name);
        if (path.isEmpty())
            return tr("Cancelled.");
    } else if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.mid(1, path.size() - 2);
    if (path.isEmpty() || path.contains('"') || path.contains(QChar::Null))
        return tr("Invalid file path.");
    auto ext = QFileInfo(path).suffix().toLower();
    if (!open && ext.isEmpty()) {
        ext = svg ? "svg" : pdf ? "pdf" : "dxf";
        path += "." + ext;
    }
    if ((drawing && ext != "dxf" && ext != "dwg") || (svg && ext != "svg") || (pdf && ext != "pdf"))
        return tr("Wrong file extension for this command.");
    path = QFileInfo(path).absoluteFilePath();
    if (!open && !protectedSource_.isEmpty() && QFileInfo(protectedSource_) == QFileInfo(path))
        return tr("Partial import: use Save As with a different file name.");
    if (drawing && fileRouter_) {
        if (auto handled = fileRouter_(name, path))
            return *handled;
    }
    const auto target = filePath(path);
    auto sheet = svg || pdf ? activeLayout() : std::nullopt;
    auto result =
        open ? io_.read(target, document_) : io_.write(target, document_, sheet ? &*sheet : nullptr);
    for (const auto& diag : result.diagnostics) {
        auto detail = QString::fromStdString(diag.message);
        if (!diag.subject.empty())
            detail = QString::fromStdString(diag.subject) + ": " + detail;
        if (diag.line)
            detail = tr("Line %1: %2").arg(diag.line).arg(detail);
        if (diag.count > 1)
            detail += tr(" (count: %1)").arg(diag.count);
        emit consoleOutput(diag.level == pd::io::IoDiagnostic::Level::Error, detail);
    }
    if (!result.ok())
        return result.status == pd::io::IoStatus::BackendUnavailable
                   ? tr("DWG converter unavailable.")
                   : tr("File operation failed: %1").arg(path);
    if (drawing) {
        if (open)
            protectedSource_ = (result.diagnostics.empty() && ext != "dwg") ? QString{} : path;
        currentFile_ = path;
        savedState_ = document_.stateId();
        emit fileChanged();
    }
    if (open)
        return tr("Opened %1: %2 entities, %3 layers, %4 skipped.")
            .arg(path)
            .arg(result.entities)
            .arg(result.layers)
            .arg(result.skipped);
    return drawing ? tr("Saved: %1").arg(path) : tr("Exported: %1").arg(path);
}
QString CommandSession::runScript(QString path) {
    // The untouched tail is a single path. No shell, escape processing or simplification of spaces.
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.mid(1, path.size() - 2);
    if (path.isEmpty() || path.contains('"'))
        return tr("Use: _RUNSCRIPT \"path to file.pdl\"");
    QFile file(path);
    if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly))
        return tr("Cannot open script: %1").arg(path);
    constexpr qint64 limit = 1024 * 1024;
    auto source = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || source.size() > limit)
        return tr("Could not read script (limit: 1 MiB).");
    auto ctx = context();
    try {
        scripts_->execute(source.toStdString(), path.toStdString(), ctx);
    } catch (const std::exception&) {
        return tr("Script failed.");
    } // engine already sent file:line:column to stderr
    emit vocabularyChanged();
    return tr("Script completed.");
}

CommandSession::CommandSession(QObject* parent)
    : QObject(parent), editor_(document_), registry_(reservedNames()), plugins_(registry_),
      scripts_(pd::makeLispEngine(registry_)) {
    aliases_ = defaultAliases();
    subscription_ = document_.observe([this](std::span<const pd::Change> changes) {
        // Qt slots (including queued connections) may outlive the borrowed core batch.
        if (changes.front().kind == pd::Change::Kind::Reset)
            selectSpace({});
        else if (std::any_of(changes.begin(), changes.end(),
                             [](auto& c) { return c.object == pd::Change::Object::Layout; })) {
            if (!space_.isEmpty() && !layoutNames().contains(space_))
                space_.clear();
            emit spaceChanged();
        }
        emit documentChanged(QList<pd::Change>(changes.begin(), changes.end()));
    });
}

std::optional<pd::EntityId> CommandSession::addLine(pd::Vec2 start, pd::Vec2 end) {
    try {
        pd::Line line(start, editor_.track(start, end).point);
        line.traits.layer = editor_.activeLayer();
        return document_.add(line);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

namespace {
// One transaction for the whole selection so Undo removes the change as a unit.
QString restyle(pd::Document& d, const std::vector<pd::EntityId>& ids,
                const std::function<void(pd::EntityTraits&)>& change) {
    if (ids.empty())
        return CommandSession::tr("Select objects first.");
    const auto before = ids.size();
    std::size_t locked = 0;
    d.transact([&](pd::Transaction& tx) {
        for (auto id : ids) {
            auto e = d.entity(id);
            if (!e)
                continue;
            if (auto layer = d.layer(e->traits.layer); layer && layer->isLocked) {
                ++locked;
                continue;
            }
            auto copy = e->clone();
            change(copy->traits);
            tx.replace(id, *copy);
        }
    });
    if (locked)
        return CommandSession::tr("%1 selected objects are on a locked layer.").arg(locked);
    return before ? CommandSession::tr("Changed %1 object(s).").arg(before)
                  : CommandSession::tr("Select objects first.");
}
} // namespace

QString CommandSession::copySelection(const std::vector<pd::EntityId>& ids, std::optional<pd::Vec2> base) {
    Clipboard next;
    for (auto id : ids)
        if (auto e = document_.entity(id))
            next.items.push_back(std::shared_ptr<pd::Entity>(e->clone().release()));
    if (next.items.empty())
        return tr("Select objects first.");
    for (auto lid : document_.layerIds())
        next.layers.push_back(*document_.layer(lid));
    next.base = base ? *base : pd::midpoint(next.items.front()->getBounds().min(),
                                            next.items.front()->getBounds().max());
    clip_ = std::move(next);
    return tr("Copied %1 object(s).").arg(clip_.items.size());
}

QString CommandSession::cutSelection(const std::vector<pd::EntityId>& ids, std::optional<pd::Vec2> base) {
    if (ids.empty())
        return tr("Select objects first.");
    auto reply = copySelection(ids, base);
    if (clip_.items.empty())
        return reply;
    std::size_t removed = 0;
    document_.transact([&](pd::Transaction& tx) {
        for (auto id : ids) {
            auto e = document_.entity(id);
            if (!e)
                continue;
            if (auto layer = document_.layer(e->traits.layer); layer && layer->isLocked)
                continue;
            tx.remove(id);
            ++removed;
        }
    });
    return tr("Cut %1 object(s).").arg(removed);
}

QString CommandSession::pasteClipboard(std::optional<pd::Vec2> at) {
    if (clip_.items.empty())
        return tr("The clipboard is empty.");
    const auto target = at ? *at : clip_.base;
    const auto shift = target - clip_.base;
    std::size_t added = 0, skipped = 0;
    try {
        document_.transact([&](pd::Transaction& tx) {
            // Layer identities travel by name, so a paste keeps the visible result of the source.
            std::map<std::string, pd::LayerId> layers;
            for (auto lid : document_.layerIds())
                layers[document_.layer(lid)->name] = lid;
            for (const auto& source : clip_.layers) {
                if (layers.contains(source.name))
                    continue;
                layers[source.name] = tx.addLayer(source);
            }
            std::vector<std::pair<pd::LayerId, pd::LayerId>> remap;
            for (const auto& source : clip_.layers) {
                const auto it = layers.find(source.name);
                if (it != layers.end())
                    remap.emplace_back(source.id(), it->second);
            }
            for (const auto& item : clip_.items) {
                if (auto ref = dynamic_cast<const pd::BlockReference*>(item.get());
                    ref && !document_.block(ref->definition().id()))
                    continue; // the block definition does not exist in this drawing
                auto copy = item->clone();
                for (auto [from, to] : remap)
                    if (copy->traits.layer == from) {
                        copy->traits.layer = to;
                        break;
                    }
                copy->transform(pd::Matrix3x3::translation(shift));
                tx.add(*copy);
                ++added;
            }
        });
    } catch (const std::exception& e) {
        return tr("Paste failed: %1").arg(QString::fromUtf8(e.what()));
    }
    return skipped ? tr("Pasted %1 object(s); %2 skipped.").arg(added).arg(skipped)
                   : tr("Pasted %1 object(s).").arg(added);
}

QString CommandSession::repeatLast() {
    if (lastCommand_.isEmpty())
        return tr("No previous command.");
    return execute("_" + lastCommand_);
}

QString CommandSession::setLayer(const std::vector<pd::EntityId>& ids, pd::LayerId layer) {
    if (!document_.layer(layer))
        return tr("Unknown layer.");
    return restyle(document_, ids, [layer](pd::EntityTraits& t) { t.layer = layer; });
}
QString CommandSession::setColor(const std::vector<pd::EntityId>& ids,
                                 std::variant<pd::ByLayer, pd::ByBlock, pd::Color> color) {
    return restyle(document_, ids, [&color](pd::EntityTraits& t) { t.color = color; });
}
QString CommandSession::setLineWeight(const std::vector<pd::EntityId>& ids,
                                      std::variant<pd::ByLayer, pd::ByBlock, double> weight) {
    return restyle(document_, ids, [&weight](pd::EntityTraits& t) { t.lineWeight = weight; });
}

std::vector<pd::EntityId> CommandSession::addLines(const std::vector<pd::Vec2>& points) {
    std::vector<pd::EntityId> ids;
    try {
        document_.transact([&](pd::Transaction& tx) {
            for (size_t i = 1; i < points.size(); ++i) {
                pd::Line line(points[i - 1], points[i]);
                line.traits.layer = editor_.activeLayer();
                ids.push_back(tx.add(line));
            }
        });
    } catch (const std::invalid_argument&) {
        return {};
    }
    return ids;
}
QString CommandSession::drawLine(pd::Vec2 first, pd::Vec2 last) {
    if (const auto id = addLine(first, last)) {
        return tr("Added line #%1.").arg(*id);
    }
    return tr("Use: _LINE x1,y1 x2,y2 (two distinct, finite points).");
}

void CommandSession::addViewportExample() {
    // Optional, deterministic scene at survey coordinates; no font or mesh baked into the document.
    document_.transact([](pd::Transaction& tx) {
        pd::Layer layer("Example");
        layer.color = {94, 213, 222};
        const auto id = tx.addLayer(layer);
        auto place = pd::Matrix3x3::translation({1e7, -1e7});
        auto add = [&](pd::Entity&& e) {
            e.traits.layer = id;
            e.transform(place);
            tx.add(e);
        };
        add(pd::Line({-70, 0}, {70, 0}));
        add(pd::Line({-40, -22}, {-20, 27}));
        add(pd::Circle({-30, 0}, 18));
        pd::Arc arc({{16, 7}, 22, -0.6, 3.6});
        arc.traits.color = pd::Color{255, 163, 94};
        add(std::move(arc));
        add(pd::Polyline({{{12, -19}, -0.65}, {{48, -21}, 0}, {{60, 5}, 0.4}}, true));
        add(pd::Text("PlaneDraft 2D", {{-68, 35}, {8, 44}}, {}));
    });
}

QString CommandSession::execute(const QString& input) {
    if (executing_)
        return tr("Another command is active.");
    QScopedValueRollback guard(executing_, true);
    const auto trimmed = input.trimmed();

    qsizetype split = 0;
    while (split < trimmed.size() && !trimmed[split].isSpace())
        ++split;
    const auto token = trimmed.left(split), tail = trimmed.mid(split).trimmed();
    if (editor_.step() == EditController::Step::LayoutSpec && !trimmed.isEmpty() && resolve(token).isEmpty())
        return layoutCommand(trimmed);
    auto selected = [this]() { return selection_ ? selection_() : std::vector<pd::EntityId>{}; };
    if (editor_.active()) {
        const bool confirm = trimmed.isEmpty() && editor_.step() == EditController::Step::Select;
        if (auto reply = editor_.tryAnswer(trimmed, confirm ? selected() : std::vector<pd::EntityId>{}))
            return *reply;
    }
    const auto name = resolve(token);
    if (editor_.active()) {
        if (name.isEmpty())
            return editor_.prompt();
        editor_.cancel(); // an explicit new command supersedes only the transient operation
    }
    if (trimmed.isEmpty())
        return {};
    if (name.isEmpty())
        return tr("Unknown command: %1").arg(token);
    lastCommand_ = name;
    recent_.removeAll(name);
    recent_.prepend(name);
    while (recent_.size() > 8)
        recent_.removeLast();
    if (name == "OPEN" || name == "SAVE" || name == "SAVEAS" || name == "EXPORTSVG" || name == "EXPORTPDF")
        return fileCommand(name, tail);
    if (name == "COPYCLIP" || name == "CUTCLIP") {
        auto ids = selected();
        if (ids.empty())
            return tr("Select objects first.");
        return name == "COPYCLIP" ? copySelection(ids) : cutSelection(ids);
    }
    if (name == "COPYBASE") {
        auto ids = selected();
        if (ids.empty())
            return tr("Select objects first.");
        if (auto p = EditController::coordinate(tail))
            return copySelection(ids, *p);
        return tr("Use: _COPYBASE x,y with objects selected.");
    }
    if (name == "PASTECLIP")
        return pasteClipboard(EditController::coordinate(tail));
    if (name == "ZOOM") {
        auto option = tail.trimmed().toUpper();
        if (!option.isEmpty() && option != "E" && option != "ВСЕ" && option != "EXTENTS")
            return tr("Use ZOOM or ZOOM E (extents).");
        if (!zoomExtents_)
            return tr("Zoom is available in the drawing window.");
        zoomExtents_();
        return tr("Zoomed to the drawing extents.");
    }
    if (name == "LAYOUT")
        return tail.isEmpty() ? editor_.start(EditController::Tool::Layout, {}) : layoutCommand(tail);
    if (!space_.isEmpty() && name != "UNDO" && name != "REDO")
        return tr("Switch to Model to edit model geometry.");
    if (name == "DIMSTYLE")
        return tail.isEmpty() ? editor_.start(EditController::Tool::DimStyle, {}) : editor_.setDimStyle(tail);
    if (name == "RUNSCRIPT")
        return runScript(tail);
    const auto words = trimmed.simplified().split(' ', Qt::SkipEmptyParts);
    if (name == "LINE") {
        if (words.size() <= 2) {
            auto msg = editor_.start(EditController::Tool::Line, {});
            return words.size() == 1 ? msg : editor_.tryAnswer(words[1], {}).value_or(msg);
        }
        // Typed points may be offsets from the one before, so the whole chain is resolved first and
        // only then drawn: "_LINE 0,0 @10,0 @0,10" is a corner, not an error.
        std::vector<pd::Vec2> chain;
        for (qsizetype i = 1; i < words.size(); ++i) {
            const auto keyword = words[i].toUpper();
            if (keyword == "C" || keyword == "З") {
                if (chain.size() < 3)
                    return tr("Use: _LINE x1,y1 x2,y2 (two distinct, finite points).");
                chain.push_back(chain.front());
                continue;
            }
            if (keyword == "U" || keyword == "ОТМ") {
                if (chain.size() > 1)
                    chain.pop_back();
                continue;
            }
            const std::optional<pd::Vec2> base =
                chain.empty() ? std::nullopt : std::optional<pd::Vec2>{chain.back()};
            const auto p = EditController::coordinate(words[i], base);
            if (!p)
                return tr("Use: _LINE x1,y1 x2,y2 (two distinct, finite points).");
            chain.push_back(*p);
        }
        const auto ids = addLines(chain);
        if (ids.empty())
            return tr("Use: _LINE x1,y1 x2,y2 (two distinct, finite points).");
        if (ids.size() == 1)
            return tr("Added line #%1.").arg(ids.front());
        return tr("Added %1 lines.").arg(static_cast<int>(ids.size()));
    }
    if (auto tool = EditController::named(name)) {
        // Construction commands accept their arguments on the command line as well as prompts:
        // _RECTANG 0,0 100,50, _POLYGON 6 0,0 C 10,0, _CIRCLE 0,0 25, _ARRAY R 2 3 100 50 ...
        if (!tail.isEmpty() && !name.startsWith("DIM") &&
            *tool != EditController::Tool::Insert && *tool != EditController::Tool::Explode &&
            *tool != EditController::Tool::Hatch && *tool != EditController::Tool::Block) {
            if (editor_.active())
                return tr("Finish the active command first.");
            auto msg = editor_.start(*tool, selected());
            if (!editor_.active())
                return msg;
            if (*tool == EditController::Tool::Array || *tool == EditController::Tool::Chamfer)
                return editor_.tryAnswer(tail, {}).value_or(msg);
            for (const auto& token : tail.split(' ', Qt::SkipEmptyParts)) {
                auto reply = editor_.tryAnswer(token, {});
                if (!reply)
                    return msg;
                msg = *reply;
                if (!editor_.active())
                    break;
            }
            return msg;
        }
        if (!tail.isEmpty() && name.startsWith("DIM")) {
            auto tokens = tail.simplified().split(' ');
            int count = name == "DIMANGULAR" ? 4 : 3;
            bool axis = name == "DIMLINEAR" && (tokens[0].toUpper() == "X" || tokens[0].toUpper() == "Y");
            if (tokens.size() != count + int(axis))
                return tr("Start the command, then answer its prompts.");
            for (auto t : tokens.mid(axis ? 1 : 0))
                if (!EditController::coordinate(t))
                    return tr("Invalid dimension point.");
            auto msg = editor_.start(*tool, {});
            for (auto t : tokens)
                msg = editor_.tryAnswer(t, {}).value_or(editor_.prompt());
            return msg;
        }
        if (!tail.isEmpty() &&
            (*tool == EditController::Tool::Insert || *tool == EditController::Tool::Explode ||
             *tool == EditController::Tool::Hatch || *tool == EditController::Tool::Block)) {
            auto msg = editor_.start(
                *tool, *tool == EditController::Tool::Explode ? std::vector<pd::EntityId>{} : selected());
            return editor_.active() ? editor_.tryAnswer(tail, {}).value_or(msg) : msg;
        }
        if (!tail.isEmpty())
            return tr("Start the command, then answer its prompts.");
        return editor_.start(*tool, selected());
    }
    if (name == "UNDO" || name == "REDO") {
        if (!tail.isEmpty())
            return tr("This command takes no arguments.");
        const auto change = name == "UNDO" ? document_.undo() : document_.redo();
        if (!change) {
            return tr("No edit to undo or redo.");
        }
        return name == "UNDO" ? tr("Edit undone.") : tr("Edit redone.");
    }
    try {
        auto ctx = context();
        auto before = registry_.entries().size();
        registry_.run(name.toStdString(), ctx, tail.toStdString());
        if (registry_.entries().size() != before)
            emit vocabularyChanged();
        return tr("Extension command completed.");
    } catch (const std::exception& e) {
        return tr("Extension error: %1").arg(QString::fromUtf8(e.what()));
    }
}
