// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/document/layer.hpp>
#include <planedraft/entities/block.hpp>
#include <planedraft/layout.hpp>

namespace pd {
namespace detail {
// Shared immutable values, not mutable aliases. Empty means that this handle does not exist.
using Record = std::variant<std::monostate, std::shared_ptr<const Entity>, std::shared_ptr<const Layer>,
                            std::shared_ptr<const BlockDefinition>, std::shared_ptr<const Layout>>;
struct Delta {
    EntityId id;
    Record before, after;
};
using Edits = std::vector<Delta>;
} // namespace detail

class Document;
class UndoStack {
  public:
    [[nodiscard]] bool canUndo() const { return cursor_ != 0; }
    [[nodiscard]] bool canRedo() const { return cursor_ < entries_.size(); }

  private:
    friend class Document;
    void preparePush(); // all possibly throwing history allocation precedes publication
    void pushPrepared(detail::Edits edits, std::uint64_t state) noexcept;
    struct Entry {
        detail::Edits edits;
        std::uint64_t state;
    };
    std::vector<Entry> entries_;
    std::size_t cursor_ = 0;
};
} // namespace pd
