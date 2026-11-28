// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/document.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pd {
namespace {
class WriteGuard {
  public:
    explicit WriteGuard(bool& busy) : busy_(busy) {
        if (busy_) {
            throw std::logic_error("nested document write");
        }
        busy_ = true;
    }
    WriteGuard(const WriteGuard&) = delete;
    WriteGuard& operator=(const WriteGuard&) = delete;
    ~WriteGuard() { busy_ = false; }

  private:
    bool& busy_;
};
template <class T> const T* value(const detail::Record& record) {
    auto p = std::get_if<std::shared_ptr<const T>>(&record);
    return p ? p->get() : nullptr;
}

std::vector<Vec2> outsidePoints(const Entity* e, const BoundingBox2D& box) {
    if (!e) {
        return {};
    }
    auto points = e->getSnapPoints(SnapType::Center | SnapType::Insertion);
    std::erase_if(points, [&](Vec2 p) { return box.contains(p); });
    return points;
}

Change::Object objectType(const detail::Record& record) {
    if (value<Entity>(record)) {
        return Change::Object::Entity;
    }
    if (value<Layout>(record))
        return Change::Object::Layout;
    return value<Layer>(record) ? Change::Object::Layer : Change::Object::Block;
}
} // namespace

void UndoStack::preparePush() {
    if (cursor_ == entries_.capacity()) {
        if (cursor_ == entries_.max_size()) {
            throw std::length_error("undo history exhausted");
        }
        entries_.reserve(cursor_ + std::min(cursor_ + 1, entries_.max_size() - cursor_));
    }
}

void UndoStack::pushPrepared(detail::Edits edits, std::uint64_t state) noexcept {
    entries_.resize(cursor_);
    entries_.push_back({std::move(edits), state});
    ++cursor_;
}

Document::Document(std::unique_ptr<SpatialIndex> index)
    : slots_{std::make_shared<const Layer>()}, index_(std::move(index)) {
    if (!index_) {
        throw std::invalid_argument("document requires a spatial index");
    }
}

const detail::Record& Document::record(EntityId id) const {
    static const detail::Record empty;
    return id < slots_.size() ? slots_[static_cast<std::size_t>(id)] : empty;
}

EntityId Document::issueId() {
    if (nextId_ >= slots_.max_size()) {
        throw std::length_error("document handles exhausted");
    }
    return nextId_++;
}

const Entity* Document::entity(EntityId id) const { return value<Entity>(record(id)); }
const Layer* Document::layer(LayerId id) const { return value<Layer>(record(id)); }
std::shared_ptr<const BlockDefinition> Document::block(BlockId id) const {
    auto b = std::get_if<std::shared_ptr<const BlockDefinition>>(&record(id));
    return b ? *b : nullptr;
}

std::vector<LayerId> Document::layerIds() const {
    std::vector<LayerId> ids;
    for (LayerId id = 0; id < slots_.size(); ++id)
        if (layer(id))
            ids.push_back(id);
    return ids;
}

std::vector<EntityId> Document::entityIds() const {
    std::vector<EntityId> ids;
    ids.reserve(size());
    for (EntityId id = 1; id < slots_.size(); ++id) {
        if (entity(id)) {
            ids.push_back(id);
        }
    }
    return ids;
}

bool Document::visible(const Entity& e) const {
    auto l = layer(e.traits.layer);
    return e.traits.isVisible && l && l->isVisible && !l->isFrozen;
}

StrokeStyle Document::resolveStyle(const Entity& e, const StrokeStyle* parent,
                                   const Layer* effectiveLayer) const {
    const Layer* l = effectiveLayer ? effectiveLayer : layer(e.traits.layer);
    if (!l) {
        throw std::out_of_range("unknown entity layer");
    }
    auto resolve = [&]<class T>(const Property<T>& prop, const T& byLayer, T StrokeStyle::*member) -> T {
        if (auto explicitValue = std::get_if<T>(&prop)) {
            return *explicitValue;
        }
        return parent && std::holds_alternative<ByBlock>(prop) ? parent->*member : byLayer;
    };
    return {resolve(e.traits.color, l->color, &StrokeStyle::color),
            resolve(e.traits.lineType, l->lineType, &StrokeStyle::lineType),
            resolve(e.traits.lineWeight, l->lineWeight, &StrokeStyle::lineWeight)};
}

std::vector<EntityId> Document::query(const BoundingBox2D& region, bool visibleOnly) const {
    auto ids = index_->query(region);
    if (visibleOnly) {
        std::erase_if(ids, [&](EntityId id) {
            auto* e = entity(id);
            if (!e) {
                throw std::logic_error("index returned an unknown entity");
            }
            return !visible(*e);
        });
    }
    return ids;
}

std::vector<EntityId> Document::queryExtraPoints(const BoundingBox2D& region) const {
    auto ids = extraPoints_->query(region);
    std::erase_if(ids, [&](EntityId id) { return !visible(*entity(id)); });
    return ids;
}

const Layout* Document::layout(EntityId id) const { return value<Layout>(record(id)); }
std::vector<EntityId> Document::layoutIds() const {
    std::vector<EntityId> ids;
    for (size_t i = 0; i < slots_.size(); ++i)
        if (layout(i))
            ids.push_back(i);
    return ids;
}
std::vector<BlockId> Document::blockIds() const {
    std::vector<BlockId> ids;
    for (size_t i = 0; i < slots_.size(); ++i)
        if (value<BlockDefinition>(slots_[i]))
            ids.push_back(i);
    return ids;
}
bool Document::hasBlockDefinitions() const {
    return std::any_of(slots_.begin(), slots_.end(),
                       [](const auto& r) { return value<BlockDefinition>(r) != nullptr; });
}
void Document::replaceContents(std::unique_ptr<Document> staging) {
    WriteGuard guard(writing_);
    if (!staging || staging->writing_)
        throw std::invalid_argument("expected finished staging document");
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("revision exhausted");
    slots_.swap(staging->slots_);
    index_.swap(staging->index_);
    extraPoints_.swap(staging->extraPoints_);
    // The linetypes of the drawing that just loaded, in place of the ones of the drawing before it.
    lineTypes_.swap(staging->lineTypes_);
    nextId_ = staging->nextId_;
    entityCount_ = staging->entityCount_;
    history_ = UndoStack{};
    const Change reset{Change::Kind::Reset, 0, ++revision_, Change::Object::Document};
    baseState_ = revision_;
    notify({&reset, 1});
}
void Document::transact(const std::function<void(Transaction&)>& build) {
    WriteGuard guard(writing_);
    detail::Edits edits;
    {
        Transaction tx(*this);
        build(tx);
        edits = std::move(tx.edits_);
    } // Release the staging lookup table before building index pages for large imports.
    std::erase_if(edits, [](const detail::Delta& d) { return d.before == d.after; });
    if (edits.empty()) {
        return;
    }
    history_.preparePush();
    auto changes = apply(edits, false);
    history_.pushPrepared(std::move(edits), revision_);
    notify(changes);
}

EntityId Document::add(const Entity& prototype) {
    EntityId id = 0;
    transact([&](Transaction& tx) { id = tx.add(prototype); });
    return id;
}

void Document::transform(EntityId id, const Matrix3x3& m) {
    transact([&](Transaction& tx) { tx.transform(id, m); });
}

void Document::remove(EntityId id) {
    transact([&](Transaction& tx) { tx.remove(id); });
}

bool Document::undo() {
    WriteGuard guard(writing_);
    if (!canUndo()) {
        return false;
    }
    auto changes = apply(history_.entries_[history_.cursor_ - 1].edits, true);
    --history_.cursor_;
    notify(changes);
    return true;
}

bool Document::redo() {
    WriteGuard guard(writing_);
    if (!canRedo()) {
        return false;
    }
    auto changes = apply(history_.entries_[history_.cursor_].edits, false);
    ++history_.cursor_;
    notify(changes);
    return true;
}

std::vector<Change> Document::apply(const detail::Edits& edits, bool reverse) {
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("revision exhausted");
    }
    auto staged = index_->fork();
    if (!staged) {
        throw std::logic_error("null spatial index fork");
    }
    auto points = extraPoints_->fork();
    std::vector<Change> changes;
    changes.reserve(edits.size());
    auto count = entityCount_;
    EntityId largest = 0;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const auto& d = edits[reverse ? edits.size() - 1 - i : i];
        const auto& from = reverse ? d.after : d.before;
        const auto& to = reverse ? d.before : d.after;
        auto old = value<Entity>(from), next = value<Entity>(to);
        auto oldBox = old ? old->getBounds() : BoundingBox2D{},
             newBox = next ? next->getBounds() : BoundingBox2D{};
        if (oldBox != newBox) {
            if (old && !staged->erase(d.id, oldBox)) {
                throw std::logic_error("document/index mismatch");
            }
            if (next) {
                staged->insert(d.id, newBox);
            }
        }
        const auto before = outsidePoints(old, oldBox), after = outsidePoints(next, newBox);
        if (before != after) {
            for (auto p : before) {
                if (!points->erase(d.id, {p, p})) {
                    throw std::logic_error("document/construction point index mismatch");
                }
            }
            for (auto p : after) {
                points->insert(d.id, {p, p});
            }
        }
        if (old && !next) {
            --count;
        }
        if (!old && next) {
            ++count;
        }
        const bool added = std::holds_alternative<std::monostate>(from),
                   removed = std::holds_alternative<std::monostate>(to);
        changes.push_back({added     ? Change::Kind::Added
                           : removed ? Change::Kind::Removed
                                     : Change::Kind::Modified,
                           d.id, revision_ + 1, objectType(removed ? from : to)});
        largest = std::max(largest, d.id);
    }
    if (largest >= slots_.size()) {
        slots_.resize(static_cast<std::size_t>(largest) + 1);
    }
    // All throwing work is complete. Slot assignments only copy monostate/shared_ptr values.
    for (const auto& d : edits) {
        slots_[static_cast<std::size_t>(d.id)] = reverse ? d.before : d.after;
    }
    index_.swap(staged);
    extraPoints_.swap(points);
    entityCount_ = count;
    ++revision_;
    return changes;
}

Document::Subscription Document::observe(Observer observer) {
    if (writing_) {
        throw std::logic_error("cannot subscribe during a write");
    }
    if (!observer) {
        throw std::invalid_argument("empty document observer");
    }
    std::erase_if(observers_, [](const auto& weak) { return weak.expired(); });
    auto token = std::make_shared<const Observer>(std::move(observer));
    observers_.push_back(token);
    return token;
}

void Document::notify(std::span<const Change> changes) noexcept {
    for (const auto& weak : observers_) {
        if (auto observer = weak.lock()) {
            try {
                (*observer)(changes);
            } catch (...) {
                if (!observerError_) {
                    observerError_ = std::current_exception();
                }
            }
        }
    }
}

std::exception_ptr Document::takeObserverError() noexcept { return std::exchange(observerError_, {}); }
} // namespace pd
