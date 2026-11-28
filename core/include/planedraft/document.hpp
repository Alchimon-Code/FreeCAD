// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <planedraft/document/undo_stack.hpp>
#include <planedraft/spatial/spatial_index.hpp>

#include <exception>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace pd {
struct Change {
    enum class Kind { Added, Modified, Removed, Reset };
    enum class Object { Entity, Layer, Block, Document, Layout };
    Kind kind;
    EntityId id;
    std::uint64_t revision;
    Object object;
};

// Staging area scoped to one synchronous Document::transact callback. Later operations in the
// callback see earlier ones; queries on Document still see the previously committed revision.
class Transaction {
  public:
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    [[nodiscard]] const Entity* entity(EntityId id) const;
    [[nodiscard]] const Layer* layer(LayerId id) const;
    EntityId add(const Entity& prototype);
    void replace(EntityId id, const Entity& replacement);
    void transform(EntityId id, const Matrix3x3& mat);
    void remove(EntityId id);
    EntityId addLayout(Layout);
    void updateLayout(EntityId, Layout);
    LayerId addLayer(Layer layer);
    void removeLayer(LayerId);
    void updateLayer(LayerId id, Layer layer);
    std::shared_ptr<const BlockDefinition> defineBlock(std::string name,
                                                       std::span<const Entity* const> members);

  private:
    friend class Document;
    explicit Transaction(Document& doc) : doc_(doc) {}
    [[nodiscard]] const detail::Record& current(EntityId id) const;
    [[nodiscard]] const Entity& editable(EntityId id) const;
    void freezeLayout(Layout&) const;
    void validateEntity(const Entity& e) const;
    void checkName(const std::string& name, Change::Object type, EntityId except) const;
    void store(EntityId id, detail::Record after);
    Document& doc_;
    detail::Edits edits_;
    std::unordered_map<EntityId, std::size_t> touched_;
};

// Single writer. Returned Entity/Layer pointers are borrowed until the next successful edit.
class Document {
  public:
    // An injected index must be empty, and fork() must isolate subsequent edits.
    explicit Document(std::unique_ptr<SpatialIndex> index = makeRTree());
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    [[nodiscard]] const Entity* entity(EntityId id) const;
    [[nodiscard]] const Layer* layer(LayerId id) const;
    [[nodiscard]] std::shared_ptr<const BlockDefinition> block(BlockId id) const;
    [[nodiscard]] std::size_t size() const { return entityCount_; }
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    [[nodiscard]] std::vector<LayerId> layerIds() const;
    [[nodiscard]] std::vector<EntityId> entityIds() const; // full enumeration in handle order
    [[nodiscard]] std::vector<EntityId> query(const BoundingBox2D& region, bool visibleOnly = true) const;
    [[nodiscard]] BoundingBox2D extents() const { return index_->bounds(); } // all geometry, including hidden
    // Only construction centres/insertion points outside their owner's geometric AABB; never inflate
    // fit/culling bounds.
    [[nodiscard]] std::vector<EntityId> queryExtraPoints(const BoundingBox2D& region) const;
    [[nodiscard]] bool visible(const Entity& entity) const;
    // For a block member, pass the containing reference's resolved stroke. Top-level ByBlock falls back to
    // its layer.
    [[nodiscard]] StrokeStyle resolveStyle(const Entity& entity, const StrokeStyle* parent = nullptr,
                                           const Layer* effectiveLayer = nullptr) const;

    // Dash patterns of the drawing as it carried them: a linetype is a run of lengths along the curve,
    // positive for ink and negative for a gap. They belong to the drawing, not to an edit, so they are
    // set when it is loaded and never go through a transaction.
    using LineTypeTable = std::map<std::string, std::vector<double>>;
    [[nodiscard]] const LineTypeTable& lineTypes() const { return lineTypes_; }
    void setLineTypes(LineTypeTable table) { lineTypes_ = std::move(table); }
    // Empty when the drawing carries no pattern under that name, which is when the viewer falls back to
    // the standard names it knows.
    [[nodiscard]] std::span<const double> lineTypePattern(const std::string& name) const {
        auto it = lineTypes_.find(name);
        return it == lineTypes_.end() ? std::span<const double>{} : std::span<const double>(it->second);
    }

    // New drawing/ID namespace. Publish a completely built model and both indexes in one Reset;
    // keep observers and the Document address, but neither old nor staging Undo/Redo history.
    void replaceContents(std::unique_ptr<Document> staging);
    const Layout* layout(EntityId) const;
    std::vector<EntityId> layoutIds() const;
    std::vector<BlockId> blockIds() const;
    [[nodiscard]] bool hasBlockDefinitions() const;
    void transact(const std::function<void(Transaction&)>& build);
    EntityId add(const Entity& prototype);
    void transform(EntityId id, const Matrix3x3& mat);
    void remove(EntityId id);
    // Unlike revision(), this identity returns to the saved state on undo, even across branches.
    [[nodiscard]] std::uint64_t stateId() const {
        return history_.cursor_ ? history_.entries_[history_.cursor_ - 1].state : baseState_;
    }
    [[nodiscard]] bool canUndo() const { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const { return history_.canRedo(); }
    bool undo();
    bool redo();

    using Observer = std::function<void(std::span<const Change>)>;
    // Keep this token alive to receive batches; reset/destroy it to unsubscribe, even in a callback.
    using Subscription = std::shared_ptr<const Observer>;
    [[nodiscard]] Subscription observe(Observer observer);
    // Observer failures cannot roll back a published edit or silence other observers.
    std::exception_ptr takeObserverError() noexcept;

  private:
    friend class Transaction;
    [[nodiscard]] const detail::Record& record(EntityId id) const;
    EntityId issueId();
    std::vector<Change> apply(const detail::Edits& edits, bool reverse);
    void notify(std::span<const Change> changes) noexcept;
    LineTypeTable lineTypes_;
    std::vector<detail::Record> slots_;
    std::unique_ptr<SpatialIndex> index_;
    std::unique_ptr<SpatialIndex> extraPoints_ = makeRTree();
    UndoStack history_;
    std::uint64_t baseState_ = 0;
    EntityId nextId_ = 1;
    std::uint64_t revision_ = 0;
    std::size_t entityCount_ = 0;
    bool writing_ = false;
    std::vector<std::weak_ptr<const Observer>> observers_;
    std::exception_ptr observerError_;
};
} // namespace pd
