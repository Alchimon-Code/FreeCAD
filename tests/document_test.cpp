// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/document.hpp>
#include <planedraft/entities/primitives.hpp>

#include <gtest/gtest.h>
#include <numbers>
#include <stdexcept>

namespace {
using namespace pd;

// End-to-end SDK example: two entities move as one edit; the index follows undo AND redo.
TEST(Document, MoveGroupAndFindItByArea) {
    Document doc;
    auto line = doc.add(Line({0, 0}, {2, 0}));
    auto circle = doc.add(Circle({0, 4}, 1));
    BoundingBox2D oldArea({-1, -1}, {3, 1}), newArea({19, -1}, {23, 1});
    doc.transact([&](Transaction& tx) {
        for (auto id : {line, circle}) {
            tx.transform(id, Matrix3x3::translation({20, 0}));
        }
    });
    EXPECT_TRUE(doc.query(oldArea).empty());
    EXPECT_EQ(doc.query(newArea), std::vector<EntityId>{line});
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.query(oldArea), std::vector<EntityId>{line});
    EXPECT_TRUE(doc.query(newArea).empty());
    EXPECT_EQ(dynamic_cast<const Circle&>(*doc.entity(circle)).center(), (Vec2{0, 4}));
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(dynamic_cast<const Circle&>(*doc.entity(circle)).center(), (Vec2{20, 4}));
    EXPECT_EQ(doc.query(newArea), std::vector<EntityId>{line});
}

TEST(Document, BulgedPolylineRemainsFindableOutsideItsChordAfterMirrorAndUndo) {
    Document doc;
    auto id = doc.add(Polyline({{{0, 0}, 2}, {{2, 0}, 0}}));
    BoundingBox2D below({0.9, -2.1}, {1.1, -1.9}), above({0.9, 1.9}, {1.1, 2.1});
    EXPECT_EQ(doc.query(below), std::vector<EntityId>{id});
    doc.transform(id, Matrix3x3::reflection({0, 0}, {1, 0}));
    EXPECT_TRUE(doc.query(below).empty());
    EXPECT_EQ(doc.query(above), std::vector<EntityId>{id});
    ASSERT_TRUE(doc.undo());
    EXPECT_TRUE(doc.query(above).empty());
    EXPECT_EQ(doc.query(below), std::vector<EntityId>{id});
}

TEST(Document, FailedMacroPreservesGeometryRevisionIndexAndRedoBranch) {
    Document doc;
    EXPECT_FALSE(doc.undo());
    EXPECT_FALSE(doc.redo());
    auto line = doc.add(Line({1e10 + 0.25, 0}, {1e10 + 0.5, -3.5}));
    auto circle = doc.add(Circle({1, 2}, 3));
    auto discarded = doc.add(Circle({70, 0}, 2));
    ASSERT_TRUE(doc.undo());
    const auto rev = doc.revision();
    auto token = doc.observe([](auto) { FAIL() << "aborted transactions must not publish"; });
    EntityId abandoned = 0;
    EXPECT_THROW(doc.transact([&](Transaction& tx) {
        abandoned = tx.add(Line({0, 0}, {2, 0}));
        tx.remove(line);
        tx.transform(circle, Matrix3x3::scaling(1, 2)); // circular geometry would become an ellipse
    }),
                 std::invalid_argument);
    EXPECT_TRUE(doc.canRedo());
    EXPECT_EQ(doc.revision(), rev);
    EXPECT_EQ(doc.size(), 2u);
    EXPECT_EQ(doc.entity(abandoned), nullptr);
    EXPECT_EQ(doc.query({{1e10, -4}, {1e10 + 1, 0}}), std::vector<EntityId>{line});
    EXPECT_EQ(dynamic_cast<const Line&>(*doc.entity(line)).start(), (Vec2{1e10 + 0.25, 0}));
    token.reset();
    auto newer = doc.add(Circle({90, 0}, 2));
    EXPECT_GT(newer, abandoned);
    EXPECT_GT(newer, discarded);
    EXPECT_FALSE(doc.redo());
}

TEST(Document, BatchObserverSeesOnlyTheCommittedState) {
    Document doc;
    std::vector<std::vector<Change>> batches;
    std::vector<std::size_t> sizes;
    auto token = doc.observe([&](std::span<const Change> changes) {
        batches.emplace_back(changes.begin(), changes.end());
        sizes.push_back(doc.size());
        for (const auto& c : changes) {
            EXPECT_EQ(c.revision, doc.revision());
            EXPECT_EQ(doc.entity(c.id) == nullptr, c.kind == Change::Kind::Removed);
        }
        EXPECT_THROW(doc.undo(), std::logic_error); // reads OK, nested writes not OK
    });
    EntityId a = 0, b = 0;
    doc.transact([&](Transaction& tx) {
        a = tx.add(Line({0, 2}, {4, 2}));
        b = tx.add(Circle({4, 7}, 2));
        tx.transform(a, Matrix3x3::translation({5, 0}));
        auto ephemeral = tx.add(Circle({0, 0}, 1));
        tx.remove(ephemeral);
        EXPECT_EQ(doc.size(), 0u);
        EXPECT_FALSE(doc.canUndo());
    });
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].size(), 2u);
    EXPECT_EQ(batches[0][0].kind, Change::Kind::Added);
    doc.transact([&](Transaction& tx) {
        tx.replace(a, Circle({8, 0}, 3));
        tx.remove(b);
    });
    ASSERT_EQ(batches.size(), 2u);
    ASSERT_EQ(batches[1].size(), 2u);
    EXPECT_EQ(batches[1][0].kind, Change::Kind::Modified);
    EXPECT_EQ(batches[1][1].kind, Change::Kind::Removed);
    ASSERT_TRUE(doc.undo());
    ASSERT_EQ(batches.size(), 3u);
    ASSERT_EQ(batches[2].size(), 2u);
    EXPECT_EQ(batches[2][0].id, b);
    EXPECT_EQ(batches[2][0].kind, Change::Kind::Added);
    EXPECT_EQ(sizes, (std::vector<std::size_t>{2, 1, 2}));
    auto rev = doc.revision();
    doc.transact([](Transaction& tx) {
        auto id = tx.add(Circle({0, 0}, 2));
        tx.remove(id);
    });
    EXPECT_EQ(doc.revision(), rev);
    EXPECT_TRUE(doc.canRedo());
    token.reset();
    doc.redo();
    EXPECT_EQ(batches.size(), 3u);
}

TEST(Document, ObserverFailureIsNotAnEditFailure) {
    Document doc;
    unsigned seen = 0;
    auto throwing = doc.observe([](auto) { throw std::runtime_error("subscriber failed"); });
    auto other = doc.observe([&](auto) { ++seen; });
    doc.add(Line({0, 0}, {1, 0}));
    EXPECT_EQ(seen, 1u);
    EXPECT_EQ(doc.size(), 1u);
    EXPECT_TRUE(doc.takeObserverError());
    EXPECT_FALSE(doc.takeObserverError());
    EXPECT_TRUE(doc.undo());
}

TEST(Document, LayerVisibilityLockAndTraitsSurviveHistory) {
    Document doc;
    LayerId layer = 0;
    EntityId id = 0;
    Line original({-1, 0}, {1, 0});
    doc.transact([&](Transaction& tx) {
        layer = tx.addLayer(Layer{"Walls"});
        original.traits.layer = layer;
        original.traits.color = ByBlock{};
        original.traits.lineWeight = 0.35;
        original.traits.isSelected = true;
        id = tx.add(original);
    });
    ASSERT_EQ(doc.layer(layer)->id(), layer);
    const StrokeStyle parent{{12, 23, 34}, "Dash", 0.7};
    auto stroke = doc.resolveStyle(*doc.entity(id), &parent);
    EXPECT_EQ(stroke.color, parent.color);
    EXPECT_EQ(stroke.lineType, "Continuous");
    EXPECT_DOUBLE_EQ(stroke.lineWeight, 0.35);
    // No mutable alias from the prototype into published data.
    original.transform(Matrix3x3::translation({9, 0}));
    auto query = BoundingBox2D({-1, -1}, {1, 1});
    EXPECT_EQ(doc.query(query), std::vector<EntityId>{id});
    auto state = *doc.layer(layer);
    state.isFrozen = true;
    state.isLocked = true;
    doc.transact([&](Transaction& tx) { tx.updateLayer(layer, state); });
    EXPECT_TRUE(doc.query(query).empty());
    EXPECT_EQ(doc.query(query, false), std::vector<EntityId>{id});
    EXPECT_THROW(doc.remove(id), std::invalid_argument);
    EXPECT_THROW(doc.transform(id, Matrix3x3{}), std::invalid_argument);
    EXPECT_THROW(doc.add(original), std::invalid_argument);
    EXPECT_THROW(doc.transact([&](Transaction& tx) { tx.addLayer(state); }), std::invalid_argument);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.query(query), std::vector<EntityId>{id});
    auto copy = doc.entity(id)->clone();
    EXPECT_TRUE(copy->traits.isSelected);
    EXPECT_TRUE(std::holds_alternative<ByBlock>(copy->traits.color));
    EXPECT_DOUBLE_EQ(std::get<double>(copy->traits.lineWeight), 0.35);
    copy->traits.isVisible = false;
    doc.transact([&](Transaction& tx) { tx.replace(id, *copy); });
    EXPECT_TRUE(doc.query(query).empty());
    ASSERT_TRUE(doc.undo());
    copy->traits.layer = 999;
    EXPECT_THROW(doc.add(*copy), std::invalid_argument);
    copy->traits.layer = 0;
    copy->traits.lineWeight = -1.0;
    EXPECT_THROW(doc.add(*copy), std::invalid_argument);
}

TEST(Document, ImmutableBlockReferencesHaveWorldBoundsAndLocalMemberHandles) {
    Document doc;
    Line member({0, 0}, {2, 0});
    EntityId id = 0;
    std::shared_ptr<const BlockDefinition> def;
    doc.transact([&](Transaction& tx) {
        const Entity* members[]{&member};
        def = tx.defineBlock("symbol", members);
        id = tx.add(BlockReference(def, {10, 0}, {2, 1}, std::numbers::pi / 2));
    });
    EXPECT_EQ(doc.block(def->id()), def);
    EXPECT_NE(def->members()[0]->id(), id);
    EXPECT_EQ(doc.size(), 1u);
    EXPECT_EQ(doc.query({{9, 3}, {11, 4}}), std::vector<EntityId>{id});
    EXPECT_TRUE(doc.query({{0, 0}, {2, 1}}).empty());
    Document foreign;
    EXPECT_THROW(foreign.add(BlockReference(def, {})), std::invalid_argument);
    ASSERT_TRUE(doc.undo());
    EXPECT_FALSE(doc.block(def->id()));
    EXPECT_FALSE(doc.entity(id));
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(doc.block(def->id()), def);
    EXPECT_TRUE(doc.entity(id));
}

TEST(Document, ThousandInsertionsAreOneUndoStep) {
    Document doc;
    auto token = doc.observe([](auto changes) { EXPECT_EQ(changes.size(), 1000u); });
    doc.transact([](Transaction& tx) {
        for (int i = 0; i < 1000; ++i) {
            tx.add(Circle({double(i) * 3, 0}, 1));
        }
    });
    ASSERT_EQ(doc.revision(), 1u);
    EXPECT_EQ(doc.query({{-1, -1}, {3000, 1}}).size(), 1000u);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.size(), 0u);
    EXPECT_FALSE(doc.canUndo());
    EXPECT_TRUE(doc.query({{-1, -1}, {3000, 1}}).empty());
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(doc.size(), 1000u);
}

// An injected write failure after the old AABB was already removed from the staged index.
class FailingIndex final : public SpatialIndex {
  public:
    FailingIndex(bool& fail, std::unique_ptr<SpatialIndex> tree = makeRTree())
        : fail_(fail), tree_(std::move(tree)) {}
    std::unique_ptr<SpatialIndex> fork() const override {
        return std::make_unique<FailingIndex>(fail_, tree_->fork());
    }
    void insert(EntityId id, const BoundingBox2D& b) override {
        if (fail_) {
            throw std::bad_alloc();
        }
        tree_->insert(id, b);
    }
    BoundingBox2D bounds() const override { return tree_->bounds(); }
    bool erase(EntityId id, const BoundingBox2D& b) override { return tree_->erase(id, b); }
    std::vector<EntityId> query(const BoundingBox2D& b, const Matrix3x3* placement) const override {
        return tree_->query(b, placement);
    }

  private:
    bool& fail_;
    std::unique_ptr<SpatialIndex> tree_;
};
TEST(Document, IndexAllocationFailureDoesNotPublishPartialCommitOrUndo) {
    bool fail = false;
    Document doc(std::make_unique<FailingIndex>(fail));
    auto id = doc.add(Circle({0, 0}, 1));
    auto m = Matrix3x3::translation({10, 0});
    fail = true;
    EXPECT_THROW(doc.transform(id, m), std::bad_alloc);
    EXPECT_EQ(doc.revision(), 1u);
    EXPECT_EQ(doc.query({{-1, -1}, {1, 1}}), std::vector<EntityId>{id});
    fail = false;
    doc.transform(id, m);
    fail = true;
    EXPECT_THROW(doc.undo(), std::bad_alloc);
    EXPECT_EQ(doc.query({{9, -1}, {11, 1}}), std::vector<EntityId>{id});
    EXPECT_FALSE(doc.canRedo());
    EXPECT_EQ(doc.revision(), 2u);
    fail = false;
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.query({{-1, -1}, {1, 1}}), std::vector<EntityId>{id});
}
} // namespace
namespace {
TEST(Document, RemovingALayerChecksStagedAndCommittedModelBlockPaperAndViewportUses) {
    using namespace pd;
    for (int kind = 0; kind < 4; ++kind) {
        Document d;
        LayerId layer;
        d.transact([&](Transaction& t) { layer = t.addLayer(Layer("Ink")); });
        auto add = [&](Transaction& t) {
            Line line({0, 0}, {1, 1});
            line.traits.layer = layer;
            if (kind == 0)
                t.add(line);
            else if (kind == 1) {
                const Entity* members[] = {&line};
                t.defineBlock("uninserted", members);
            } else {
                Layout l;
                if (kind == 2)
                    l.paperEntities.push_back(line.clone());
                else {
                    l.viewports.emplace_back(BoundingBox2D({30, 30}, {60, 90}), Vec2{}, 1);
                    l.viewports.back().traits.layer = layer;
                }
                t.addLayout(l);
            }
        };
        auto state = d.stateId();
        EXPECT_THROW(d.transact([&](Transaction& t) {
            add(t);
            t.removeLayer(layer);
        }),
                     std::invalid_argument);
        EXPECT_EQ(d.stateId(), state);
        d.transact(add);
        EXPECT_THROW(d.transact([&](Transaction& t) { t.removeLayer(layer); }), std::invalid_argument);
        EXPECT_TRUE(d.undo());
        EXPECT_EQ(d.stateId(), state);
        d.transact([&](Transaction& t) { t.removeLayer(layer); });
        EXPECT_FALSE(d.layer(layer));
        EXPECT_FALSE(d.canRedo());
    }
    Document d;
    EXPECT_THROW(d.transact([](Transaction& t) { t.removeLayer(DefaultLayer); }), std::invalid_argument);
    EXPECT_THROW(d.transact([](Transaction& t) { t.updateLayer(DefaultLayer, Layer("renamed")); }),
                 std::invalid_argument);
    EXPECT_FALSE(d.canUndo());
}
TEST(Document, SavePointIdentitySurvivesUndoRedoButNotBranchReplacement) {
    using namespace pd;
    Document d;
    auto initial = d.stateId();
    d.add(Line({0, 0}, {1, 0}));
    auto saved = d.stateId();
    d.add(Circle({}, 1));
    auto discarded = d.stateId();
    ASSERT_TRUE(d.undo());
    EXPECT_EQ(d.stateId(), saved);
    ASSERT_TRUE(d.redo());
    EXPECT_EQ(d.stateId(), discarded);
    d.undo();
    d.undo();
    EXPECT_EQ(d.stateId(), initial);
    d.add(Line({0, 1}, {1, 1}));
    EXPECT_NE(d.stateId(), saved);
    EXPECT_NE(d.stateId(), discarded);
    d.replaceContents(std::make_unique<Document>());
    auto base = d.stateId();
    EXPECT_NE(base, initial);
    d.add(Circle({}, 5));
    d.undo();
    EXPECT_EQ(d.stateId(), base);
}
} // namespace
