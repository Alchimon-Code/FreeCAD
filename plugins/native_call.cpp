// SPDX-License-Identifier: LGPL-2.1-or-later
#include "native_call.hpp"
#include "geometry_abi.hpp"
#include "utf8.hpp"

#include <atomic>
#include <cstdio>
#include <stdexcept>

namespace pd::abi {
std::string statusText(PdStatus status) {
    switch (status) {
    case PD_OK:
        return "PD_OK";
    case PD_ERROR:
        return "PD_ERROR";
    case PD_INVALID_ARGUMENT:
        return "PD_INVALID_ARGUMENT";
    case PD_NOT_FOUND:
        return "PD_NOT_FOUND";
    case PD_BAD_STATE:
        return "PD_BAD_STATE";
    case PD_UNSUPPORTED:
        return "PD_UNSUPPORTED";
    case PD_NO_MEMORY:
        return "PD_NO_MEMORY";
    case PD_CONFLICT:
        return "PD_CONFLICT";
    default:
        return "unknown status " + std::to_string(status);
    }
}
namespace {
struct Call;
thread_local Call* active = nullptr;
std::atomic<uint64_t> nextToken{1};
enum class State { Idle, Writing, Sealed, Cancelled };
struct Call {
    ExtensionContext& ctx;
    Transaction& tx;
    PdDocumentHandle doc{nextToken.fetch_add(1)};
    PdTransactionHandle handle{nextToken.fetch_add(1)};
    State state = State::Idle;
    bool poisoned = false;
    char detail[256]{};
    std::map<const void*, std::shared_ptr<void>> buffers;
    Call* previous;
    Call(ExtensionContext& c, Transaction& t) : ctx(c), tx(t), previous(active) { active = this; }
    ~Call() { active = previous; }
    bool staged() const { return state == State::Writing || state == State::Sealed; }
    void fail(const char* message) noexcept { std::snprintf(detail, sizeof detail, "%s", message); }
};
bool valid(PdDocumentHandle h) { return active && h.opaque == active->doc.opaque; }
bool valid(PdTransactionHandle h) {
    return active && h.opaque == active->handle.opaque && active->state == State::Writing;
}
void require(bool ok, const char* reason) {
    if (!ok)
        throw std::invalid_argument(reason);
}
template <class F> PdStatus guarded(F&& f) noexcept {
    try {
        f();
        return PD_OK;
    } catch (const std::bad_alloc&) {
        active->fail("allocation failed");
        return PD_NO_MEMORY;
    } catch (const std::out_of_range& e) {
        active->fail(e.what());
        return PD_NOT_FOUND;
    } catch (const std::invalid_argument& e) {
        active->fail(e.what());
        return PD_INVALID_ARGUMENT;
    } catch (const std::domain_error& e) {
        active->fail(e.what());
        return PD_UNSUPPORTED;
    } catch (...) {
        active->fail("host operation failed");
        return PD_ERROR;
    }
}
template <class F> PdStatus write(PdTransactionHandle t, F&& f) noexcept {
    if (!valid(t) || active->poisoned)
        return PD_BAD_STATE;
    auto s = guarded(f);
    active->poisoned = s != PD_OK;
    return s;
}
PdStatus PD_CALL begin(PdDocumentHandle h, PdTransactionHandle* out) noexcept {
    if (!valid(h) || active->state != State::Idle)
        return PD_BAD_STATE;
    if (!out)
        return PD_INVALID_ARGUMENT;
    active->state = State::Writing;
    *out = active->handle;
    return PD_OK;
}
PdStatus PD_CALL commit(PdTransactionHandle t) noexcept {
    if (!valid(t) || active->poisoned)
        return PD_BAD_STATE;
    active->state = State::Sealed;
    return PD_OK;
}
PdStatus PD_CALL rollback(PdTransactionHandle t) noexcept {
    if (!valid(t))
        return PD_BAD_STATE;
    active->state = State::Cancelled;
    return PD_OK;
}
PdStatus PD_CALL add(PdTransactionHandle t, const PdEntityData* d, PdId* id) noexcept {
    return write(t, [&] {
        require(d && id, "missing entity or output");
        *id = active->tx.add(*decode(*d));
    });
}
PdStatus PD_CALL layer(PdTransactionHandle t, const PdLayerData* d, PdId* id) noexcept {
    return write(t, [&] {
        require(d && id, "missing layer or output");
        *id = active->tx.addLayer(decode(*d));
    });
}
PdStatus PD_CALL transform(PdTransactionHandle t, PdId id, PdMatrix m) noexcept {
    return write(t, [&] { active->tx.transform(id, matrix(m)); });
}
PdStatus PD_CALL remove(PdTransactionHandle t, PdId id) noexcept {
    return write(t, [&] { active->tx.remove(id); });
}

template <class Storage, class Data, class T> void snapshot(const Data** out, const T* object) {
    require(out, "missing snapshot output");
    *out = nullptr;
    if (!object)
        throw std::out_of_range("object not found");
    auto storage = std::make_shared<Storage>(*object);
    auto ptr = &storage->data;
    active->buffers.emplace(ptr, std::move(storage));
    *out = ptr;
}
PdStatus PD_CALL readEntity(PdDocumentHandle h, PdId id, const PdEntityData** out) noexcept {
    if (!valid(h))
        return PD_BAD_STATE;
    return guarded([&] {
        snapshot<EntitySnapshot>(out, active->staged() ? active->tx.entity(id) : active->ctx.doc.entity(id));
    });
}
PdStatus PD_CALL readLayer(PdDocumentHandle h, PdId id, const PdLayerData** out) noexcept {
    if (!valid(h))
        return PD_BAD_STATE;
    return guarded([&] {
        snapshot<LayerSnapshot>(out, active->staged() ? active->tx.layer(id) : active->ctx.doc.layer(id));
    });
}
template <class F> PdStatus ids(PdDocumentHandle h, PdIds* out, F&& f) noexcept {
    if (!valid(h))
        return PD_BAD_STATE;
    if (!out)
        return PD_INVALID_ARGUMENT;
    *out = {};
    return guarded([&] {
        auto values = f();
        if (values.empty())
            return;
        auto storage = std::make_shared<std::vector<PdId>>(std::move(values));
        PdIds result{storage->data(), storage->size()};
        active->buffers.emplace(result.data, std::move(storage));
        *out = result;
    });
}
PdStatus PD_CALL entities(PdDocumentHandle h, PdIds* out) noexcept {
    return ids(h, out, [] { return active->ctx.doc.entityIds(); });
}
PdStatus PD_CALL layers(PdDocumentHandle h, PdIds* out) noexcept {
    return ids(h, out, [] { return active->ctx.doc.layerIds(); });
}
PdStatus PD_CALL selection(PdDocumentHandle h, PdIds* out) noexcept {
    return ids(h, out, [] { return active->ctx.selection(); });
}
PdStatus PD_CALL query(PdDocumentHandle h, PdBox b, uint32_t visible, PdIds* out) noexcept {
    return ids(h, out, [&] {
        require(visible <= 1, "invalid query flag");
        return active->ctx.doc.query(box(b), visible != 0);
    });
}
PdStatus PD_CALL release(PdDocumentHandle h, const void* ptr) noexcept {
    if (!valid(h))
        return PD_BAD_STATE;
    return !ptr || active->buffers.erase(ptr) ? PD_OK : PD_INVALID_ARGUMENT;
}
PdStatus PD_CALL log(PdDocumentHandle h, uint32_t err, const char* message) noexcept {
    if (!valid(h))
        return PD_BAD_STATE;
    return guarded([&] {
        require(message && err <= 1 && validText(message), "invalid log record");
        active->ctx.output(err != 0, message);
    });
}
struct Cancelled {};
// V1 promised one undoable edit per successful add_line, even if the next call fails. Keep
// that observable contract, not just its binary layout. V2 clients opt into grouped edits.
struct LegacyCall;
thread_local LegacyCall* legacy = nullptr;
struct LegacyCall {
    Document& doc;
    LegacyCall* previous;
    explicit LegacyCall(Document& d) : doc(d), previous(legacy) { legacy = this; }
    ~LegacyCall() { legacy = previous; }
};
PdStatus PD_CALL legacyAdd(void* context, double x1, double y1, double x2, double y2) noexcept {
    if (!legacy || context != legacy)
        return PD_ERROR;
    try {
        legacy->doc.add(Line({x1, y1}, {x2, y2}));
        return PD_OK;
    } catch (...) {
        return PD_ERROR;
    }
}
} // namespace
void invoke(PdRunV2 run, void* user, ExtensionContext& ctx, std::string_view args) {
    require(validText(args), "invalid UTF-8 arguments");
    std::string tail(args);
    try {
        ctx.doc.transact([&](Transaction& tx) {
            Call call(ctx, tx);
            const PdHostV2 host{PD_ABI_V2,  sizeof(PdHostV2),
                                call.doc,   begin,
                                commit,     rollback,
                                add,        layer,
                                transform,  remove,
                                readEntity, readLayer,
                                entities,   layers,
                                selection,  query,
                                release,    log};
            auto status = run(user, &host, tail.c_str());
            if (status != PD_OK)
                throw std::runtime_error("plugin returned " + statusText(status) +
                                         (call.detail[0] ? std::string(": ") + call.detail : ""));
            if (call.state == State::Cancelled)
                throw Cancelled{};
            if (call.poisoned || call.state == State::Writing)
                throw std::runtime_error(call.detail[0] ? call.detail : "unclosed plugin transaction");
        });
    } catch (const Cancelled&) {
    } // discard private edits; do NOT emulate rollback using Undo
}
void invoke(const PdPluginV1& plugin, ExtensionContext& ctx, std::string_view args) {
    if (!args.empty())
        throw std::invalid_argument("v1 command takes no arguments");
    LegacyCall call(ctx.doc);
    const PdHostV1 host{PD_ABI_V1, sizeof(PdHostV1), &call, legacyAdd};
    if (plugin.run(&host) != PD_OK)
        throw std::runtime_error("v1 command failed");
}
} // namespace pd::abi
