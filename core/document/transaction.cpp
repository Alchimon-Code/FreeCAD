// SPDX-License-Identifier: LGPL-2.1-or-later
#include <planedraft/drafting.hpp>
#include <set>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pd {
namespace {
void checkLayer(const Layer& layer) {
    if (layer.name.empty() || layer.lineType.empty() || !std::isfinite(layer.lineWeight) ||
        layer.lineWeight < 0) {
        throw std::invalid_argument("invalid layer properties");
    }
}

std::unique_ptr<Entity> copyOf(const Entity& source) {
    auto copy = source.clone();
    if (!copy) {
        throw std::logic_error("entity clone returned null");
    }
    return copy;
}
} // namespace

const detail::Record& Transaction::current(EntityId id) const {
    auto it = touched_.find(id);
    return it == touched_.end() ? doc_.record(id) : edits_[it->second].after;
}

const Entity* Transaction::entity(EntityId id) const {
    auto p = std::get_if<std::shared_ptr<const Entity>>(&current(id));
    return p ? p->get() : nullptr;
}
const Layer* Transaction::layer(LayerId id) const {
    auto p = std::get_if<std::shared_ptr<const Layer>>(&current(id));
    return p ? p->get() : nullptr;
}

const Entity& Transaction::editable(EntityId id) const {
    auto e = std::get_if<std::shared_ptr<const Entity>>(&current(id));
    if (!e) {
        throw std::out_of_range("unknown model entity");
    }
    if (std::get<std::shared_ptr<const Layer>>(current((*e)->traits.layer))->isLocked) {
        throw std::invalid_argument("locked layer");
    }
    return **e;
}

void Transaction::validateEntity(const Entity& e) const {
    if (dynamic_cast<const ViewportEntity*>(&e))
        throw std::invalid_argument("viewports belong to a layout");
    auto layer = std::get_if<std::shared_ptr<const Layer>>(&current(e.traits.layer));
    if (!layer || (*layer)->isLocked) {
        throw std::invalid_argument("unknown or locked target layer");
    }
    auto weight = std::get_if<double>(&e.traits.lineWeight);
    auto lineType = std::get_if<std::string>(&e.traits.lineType);
    if ((weight && (!std::isfinite(*weight) || *weight < 0)) || (lineType && lineType->empty()) ||
        e.getBounds().empty()) {
        throw std::invalid_argument("invalid entity bounds or properties");
    }
    if (auto ref = dynamic_cast<const BlockReference*>(&e)) {
        auto def = std::get_if<std::shared_ptr<const BlockDefinition>>(&current(ref->definition().id()));
        if (!def || def->get() != &ref->definition()) {
            throw std::invalid_argument("foreign block definition");
        }
    }
}

void Transaction::store(EntityId id, detail::Record after) {
    auto [pos, inserted] = touched_.try_emplace(id, edits_.size());
    if (inserted) {
        try {
            edits_.push_back({id, doc_.record(id), std::move(after)});
        } catch (...) {
            touched_.erase(pos);
            throw;
        }
    } else {
        edits_[pos->second].after = std::move(after);
    }
}

EntityId Transaction::add(const Entity& prototype) {
    auto copy = copyOf(prototype);
    validateEntity(*copy);
    const auto id = copy->id_ = doc_.issueId();
    store(id, std::shared_ptr<const Entity>(std::move(copy)));
    return id;
}

void Transaction::replace(EntityId id, const Entity& replacement) {
    (void)editable(id);
    auto copy = copyOf(replacement);
    validateEntity(*copy);
    copy->id_ = id;
    store(id, std::shared_ptr<const Entity>(std::move(copy)));
}

void Transaction::transform(EntityId id, const Matrix3x3& m) {
    auto copy = copyOf(editable(id));
    copy->transform(m);
    validateEntity(*copy);
    copy->id_ = id;
    store(id, std::shared_ptr<const Entity>(std::move(copy)));
}

void Transaction::remove(EntityId id) {
    (void)editable(id);
    store(id, {});
}

void Transaction::checkName(const std::string& name, Change::Object type, EntityId except) const {
    auto check = [&](EntityId id, const detail::Record& record) {
        if (id == except) {
            return;
        }
        if (type == Change::Object::Layer) {
            auto l = std::get_if<std::shared_ptr<const Layer>>(&record);
            if (l && (*l)->name == name) {
                throw std::invalid_argument("duplicate layer name");
            }
        } else if (type == Change::Object::Layout) {
            auto l = std::get_if<std::shared_ptr<const Layout>>(&record);
            if (l && (*l)->name == name)
                throw std::invalid_argument("duplicate layout name");
        } else {
            auto b = std::get_if<std::shared_ptr<const BlockDefinition>>(&record);
            if (b && (*b)->name() == name) {
                throw std::invalid_argument("duplicate block name");
            }
        }
    };
    for (std::size_t i = 0; i < doc_.slots_.size(); ++i) {
        if (!touched_.contains(i)) {
            check(i, doc_.slots_[i]);
        }
    }
    for (const auto& edit : edits_) {
        check(edit.id, edit.after);
    }
}

void Transaction::freezeLayout(Layout& l) const {
    l.validate();
    for (auto& vp : l.viewports)
        if (!layer(vp.traits.layer))
            throw std::invalid_argument("unknown viewport layer");
    for (auto& p : l.paperEntities) {
        if (!p)
            throw std::invalid_argument("null paper entity");
        auto copy = copyOf(*p);
        validateEntity(*copy);
        p = std::move(copy);
    }
}
EntityId Transaction::addLayout(Layout l) {
    freezeLayout(l);
    checkName(l.name, Change::Object::Layout, 0);
    auto id = doc_.issueId();
    store(id, std::make_shared<const Layout>(std::move(l)));
    return id;
}
void Transaction::updateLayout(EntityId id, Layout l) {
    if (!std::holds_alternative<std::shared_ptr<const Layout>>(current(id)))
        throw std::invalid_argument("unknown layout");
    freezeLayout(l);
    checkName(l.name, Change::Object::Layout, id);
    store(id, std::make_shared<const Layout>(std::move(l)));
}
LayerId Transaction::addLayer(Layer layer) {
    checkLayer(layer);
    checkName(layer.name, Change::Object::Layer, std::numeric_limits<EntityId>::max());
    auto id = layer.id_ = doc_.issueId();
    store(id, std::make_shared<const Layer>(std::move(layer)));
    return id;
}

void Transaction::removeLayer(LayerId id) {
    if (id == DefaultLayer || !layer(id))
        throw std::invalid_argument("cannot remove default or missing layer");
    auto check = [id](const Entity& e) {
        if (e.traits.layer == id)
            throw std::invalid_argument("layer is in use");
    };
    auto inspect = [&](const detail::Record& record) {
        if (auto p = std::get_if<std::shared_ptr<const Entity>>(&record))
            check(**p);
        if (auto b = std::get_if<std::shared_ptr<const BlockDefinition>>(&record))
            for (auto& e : (*b)->members())
                check(*e);
        if (auto l = std::get_if<std::shared_ptr<const Layout>>(&record)) {
            for (auto& e : (*l)->paperEntities)
                check(*e);
            for (auto& v : (*l)->viewports)
                check(v);
        }
    };
    // Check the staged view, including uninserted definitions and inactive paper spaces.
    for (size_t i = 0; i < doc_.slots_.size(); ++i)
        if (!touched_.contains(i))
            inspect(doc_.slots_[i]);
    for (auto& edit : edits_)
        inspect(edit.after);
    store(id, {});
}
void Transaction::updateLayer(LayerId id, Layer layer) {
    if (!std::holds_alternative<std::shared_ptr<const Layer>>(current(id))) {
        throw std::out_of_range("unknown layer");
    }
    checkLayer(layer);
    if (id == DefaultLayer && layer.name != "0")
        throw std::invalid_argument("cannot rename layer 0");
    if (this->layer(id)->name != layer.name)
        checkName(layer.name, Change::Object::Layer, id);
    layer.id_ = id;
    store(id, std::make_shared<const Layer>(std::move(layer)));
}

std::shared_ptr<const BlockDefinition> Transaction::defineBlock(std::string name,
                                                                std::span<const Entity* const> members) {
    std::set<BlockId> seen;
    auto check = [&](auto&& self, const Entity* e) -> void {
        auto r = dynamic_cast<const BlockReference*>(e);
        if (!r || !seen.insert(r->definition().id()).second)
            return;
        if (r->definition().name() == name)
            throw CircularReferenceError(name);
        for (auto& child : r->definition().members())
            self(self, child.get());
    };
    for (auto e : members)
        check(check, e);
    checkName(name, Change::Object::Block, std::numeric_limits<EntityId>::max());
    std::vector<std::unique_ptr<const Entity>> copies;
    copies.reserve(members.size());
    for (const auto* e : members) {
        if (!e) {
            throw std::invalid_argument("null block member");
        }
        auto copy = copyOf(*e);
        validateEntity(*copy);
        copy->id_ = doc_.issueId();
        copies.push_back(std::move(copy));
    }
    auto id = doc_.issueId();
    auto def =
        std::shared_ptr<const BlockDefinition>(new BlockDefinition(id, std::move(name), std::move(copies)));
    store(id, def);
    return def;
}
} // namespace pd
