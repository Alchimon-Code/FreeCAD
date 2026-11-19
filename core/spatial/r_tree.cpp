// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/spatial/spatial_index.hpp>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace pd {
namespace {
// Immutable equal-height pages. Non-root pages contain 4..8 entries. A fork shares its root;
// editing a path constructs replacement pages before publishing the new root.
constexpr std::size_t fanout = 8;
struct Page;
using PagePtr = std::shared_ptr<const Page>;
struct Branch {
    BoundingBox2D box;
    EntityId id;
    PagePtr child;
};
using Branches = std::vector<Branch>;
struct Page {
    explicit Page(Branches items) : entries(std::move(items)) {
        for (const auto& entry : entries) {
            bounds.include(entry.box);
        }
    }
    [[nodiscard]] bool leaf() const { return !entries.front().child; }
    Branches entries;
    BoundingBox2D bounds;
};
Branch subtree(const PagePtr& page) { return {page->bounds, 0, page}; }
// Quarter-scaled semiperimeter: remains representable even for [-DBL_MAX, DBL_MAX]^2.
double margin(const BoundingBox2D& box) {
    auto d = box.max() * 0.25 - box.min() * 0.25;
    return d.x + d.y;
}

std::size_t choose(const Branches& entries, const BoundingBox2D& box, std::size_t skip = fanout + 1) {
    auto cost = [&](const Branch& branch) {
        auto combined = branch.box;
        combined.include(box);
        const auto old = margin(branch.box);
        return std::tuple{margin(combined) - old, old, branch.child->entries.size()};
    };
    std::size_t best = skip == 0 ? 1 : 0;
    auto bestCost = cost(entries[best]);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i == skip) {
            continue;
        }
        auto c = cost(entries[i]);
        if (c < bestCost) {
            best = i;
            bestCost = c;
        }
    }
    return best;
}

struct Split {
    PagePtr left, right;
};
Split pack(Branches entries) {
    if (entries.size() <= fanout) {
        return {std::make_shared<Page>(std::move(entries)), {}};
    }
    // Median split on the axis with the larger spread of entry centres.
    BoundingBox2D centers;
    for (auto& e : entries) {
        centers.include(midpoint(e.box.min(), e.box.max()));
    }
    const auto extent = centers.max() * 0.5 - centers.min() * 0.5;
    auto key = [&](const Branch& e) {
        auto p = midpoint(e.box.min(), e.box.max());
        return extent.x >= extent.y ? p.x : p.y;
    };
    auto mid = entries.begin() + static_cast<std::ptrdiff_t>(entries.size() / 2);
    std::nth_element(entries.begin(), mid, entries.end(),
                     [&](const auto& a, const auto& b) { return key(a) < key(b); });
    Branches right(std::make_move_iterator(mid), std::make_move_iterator(entries.end()));
    entries.erase(mid, entries.end());
    return {std::make_shared<Page>(std::move(entries)), std::make_shared<Page>(std::move(right))};
}

Split insertAt(const PagePtr& page, const Branch& item) {
    auto entries = page->entries;
    if (page->leaf()) {
        entries.push_back(item);
    } else {
        auto i = choose(entries, item.box);
        auto [left, right] = insertAt(entries[i].child, item);
        entries[i] = subtree(left);
        if (right) {
            entries.push_back(subtree(right));
        }
    }
    return pack(std::move(entries));
}
// Restore minimum occupancy by redistributing with a sibling or merging the two pages.
void rebalance(Branches& entries, std::size_t i) {
    auto j = choose(entries, entries[i].box, i);
    auto combined = entries[i].child->entries;
    const auto& sibling = entries[j].child->entries;
    combined.insert(combined.end(), sibling.begin(), sibling.end());
    auto [left, right] = pack(std::move(combined));
    entries[i] = subtree(left);
    if (right) {
        entries[j] = subtree(right);
    } else {
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(j));
    }
}
// Returning the original page means no match. Null means a now-empty leaf.
PagePtr eraseAt(const PagePtr& page, EntityId id, const BoundingBox2D& box) {
    const auto& original = page->entries;
    for (std::size_t i = 0; i < original.size(); ++i) {
        const auto& e = original[i];
        PagePtr child;
        if (page->leaf()) {
            if (e.id != id || e.box != box) {
                continue;
            }
        } else {
            if (!e.box.contains(box)) {
                continue;
            }
            child = eraseAt(e.child, id, box);
            if (child == e.child) {
                continue;
            }
        }
        auto entries = original;
        if (!child) {
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            entries[i] = subtree(child);
            if (child->entries.size() < fanout / 2 && entries.size() > 1) {
                rebalance(entries, i);
            }
        }
        return entries.empty() ? nullptr : std::make_shared<Page>(std::move(entries));
    }
    return page;
}

void collect(const Page& page, const BoundingBox2D& region, std::vector<EntityId>& hits, const Matrix3x3* m) {
    for (const auto& entry : page.entries) {
        if (!(m ? m->applyBounds(entry.box) : entry.box).intersects(region)) {
            continue;
        }
        if (entry.child) {
            collect(*entry.child, region, hits, m);
        } else {
            hits.push_back(entry.id);
        }
    }
}

class RTree final : public SpatialIndex {
  public:
    std::unique_ptr<SpatialIndex> fork() const override { return std::make_unique<RTree>(*this); }
    BoundingBox2D bounds() const override { return root_ ? root_->bounds : BoundingBox2D{}; }
    void insert(EntityId id, const BoundingBox2D& box) override {
        if (box.empty()) {
            throw std::invalid_argument("cannot index empty bounds");
        }
        Branch item{box, id, {}};
        if (!root_) {
            root_ = std::make_shared<Page>(Branches{item});
            return;
        }
        auto [left, right] = insertAt(root_, item);
        root_ = right ? std::make_shared<Page>(Branches{subtree(left), subtree(right)}) : std::move(left);
    }
    bool erase(EntityId id, const BoundingBox2D& box) override {
        if (!root_ || !root_->bounds.contains(box)) {
            return false;
        }
        auto next = eraseAt(root_, id, box);
        if (next == root_) {
            return false;
        }
        if (next && !next->leaf() && next->entries.size() == 1) {
            auto onlyChild = next->entries.front().child;
            next = std::move(onlyChild);
        }
        root_ = std::move(next);
        return true;
    }
    std::vector<EntityId> query(const BoundingBox2D& region, const Matrix3x3* m) const override {
        std::vector<EntityId> hits;
        if (root_ && (m ? m->applyBounds(root_->bounds) : root_->bounds).intersects(region)) {
            collect(*root_, region, hits, m);
        }
        return hits;
    }

  private:
    PagePtr root_;
};
} // namespace

std::unique_ptr<SpatialIndex> makeRTree() { return std::make_unique<RTree>(); }
} // namespace pd
