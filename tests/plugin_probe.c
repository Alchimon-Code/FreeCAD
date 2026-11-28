/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <planedraft/pd_plugin_api.h>
#include <stdlib.h>
#include <string.h>

/* Deliberately request failures as well as successful calls. No private host symbols. */
#define CHECK(test)                                                                                          \
    do {                                                                                                     \
        if (!(test)) {                                                                                       \
            h->pd_log(d, 1, "probe: " #test);                                                                \
            return PD_ERROR;                                                                                 \
        }                                                                                                    \
    } while (0)
typedef struct {
    PdDocumentHandle last;
    PdId arc;
} Probe;
static PdStatus PD_CALL probe(void* user, const PdHostV2* h, const char* args) {
    Probe* p = (Probe*)user;
    PdDocumentHandle d = h->document;
    PdTransactionHandle t = {0};
    PdEntityData in = {0};
    const PdEntityData* out = NULL;
    PdIds ids = {0};
    PdId id = 0;
    if (p->last.opaque)
        CHECK(h->pd_entities(p->last, &ids) == PD_BAD_STATE);
    p->last = d;
    if (!strcmp(args, "read")) {
        CHECK(h->pd_read_entity(d, p->arc, &out) == PD_OK && out->kind == PD_ARC);
        CHECK(out->geometry.arc.radius == 4 && out->geometry.arc.start == 0 &&
              out->geometry.arc.sweep == 1.5);
        CHECK(out->color_mode == PD_EXPLICIT && out->color.r == 123 && out->linetype_mode == PD_EXPLICIT &&
              !strcmp(out->linetype, "Dashed"));
        CHECK(out->weight_mode == PD_BY_BLOCK && h->pd_free_buffer(d, out) == PD_OK);
        CHECK(h->pd_selection(d, &ids) == PD_OK && ids.count == 1 && ids.data[0] == p->arc);
        CHECK(h->pd_free_buffer(d, ids.data) == PD_OK);
        CHECK(h->pd_query_box(d, (PdBox){{0, -1}, {6, 6}}, 1, &ids) == PD_OK && ids.count == 1 &&
              ids.data[0] == p->arc);
        /* An unfreed query snapshot must also be cleaned up by the host. */
        CHECK(h->pd_read_entity(d, 999999, &out) == PD_NOT_FOUND && !out);
        CHECK(h->pd_free_buffer(d, &t) == PD_INVALID_ARGUMENT);
        return PD_OK;
    }
    CHECK(h->pd_begin_transaction(d, &t) == PD_OK);
    in.struct_size = sizeof in;
    in.kind = PD_ARC;
    in.geometry.arc.center = (PdPoint){1, 1};
    in.geometry.arc.radius = 4;
    in.geometry.arc.sweep = 1.5;
    in.color_mode = PD_EXPLICIT;
    in.color.r = 123;
    in.weight_mode = PD_BY_BLOCK;
    in.linetype_mode = PD_EXPLICIT;
    in.linetype = "Dashed";
    CHECK(h->pd_add_entity(t, &in, &id) == PD_OK);
    CHECK(h->pd_read_entity(d, id, &out) == PD_OK && out->kind == PD_ARC);
    CHECK(h->pd_free_buffer(d, out) == PD_OK);
    if (!strcmp(args, "cancel"))
        return h->pd_rollback(t);
    if (!strcmp(args, "forget"))
        return PD_OK;
    if (!strcmp(args, "poison")) {
        in.struct_size = 8;
        CHECK(h->pd_add_entity(t, &in, &id) == PD_INVALID_ARGUMENT);
        CHECK(h->pd_commit(t) == PD_BAD_STATE);
        return PD_OK;
    }
    if (!strcmp(args, "error")) {
        CHECK(h->pd_commit(t) == PD_OK);
        CHECK(h->pd_delete(t, id) == PD_BAD_STATE);
        return PD_ERROR;
    }
    if (!strcmp(args, "roundtrip")) {
        PdVertex vertices[3] = {{{2, 4}, 0.5}, {{12, 4}, 0}, {{7, 9}, -1}};
        PdId tmp;
        in.kind = PD_POLYLINE;
        in.geometry.polyline.vertices = vertices;
        in.geometry.polyline.count = 3;
        in.geometry.polyline.closed = 1;
        CHECK(h->pd_add_entity(t, &in, &tmp) == PD_OK);
        vertices[0].bulge = 0;
        CHECK(h->pd_read_entity(d, tmp, &out) == PD_OK && out->geometry.polyline.closed == 1 &&
              out->geometry.polyline.vertices[0].bulge == 0.5);
        CHECK(h->pd_free_buffer(d, out) == PD_OK);
        CHECK(h->pd_delete(t, tmp) == PD_OK);
        in.kind = PD_TEXT;
        in.geometry.text.utf8 = "Тест";
        in.geometry.text.placement = (PdMatrix){1, .2, 11, 0, 1, 22};
        in.geometry.text.local_ink = (PdBox){{1, 2}, {3, 4}};
        CHECK(h->pd_add_entity(t, &in, &tmp) == PD_OK);
        CHECK(h->pd_transform(t, tmp, (PdMatrix){1, 0, 10, 0, 1, 0}) == PD_OK);
        CHECK(h->pd_read_entity(d, tmp, &out) == PD_OK && !strcmp(out->geometry.text.utf8, "Тест") &&
              out->geometry.text.placement.tx == 21 && out->geometry.text.placement.b == .2 &&
              out->geometry.text.local_ink.max.y == 4);
        CHECK(h->pd_delete(t, tmp) == PD_OK);
        CHECK(!strcmp(out->geometry.text.utf8, "Тест"));
        CHECK(h->pd_free_buffer(d, out) == PD_OK);
        in.kind = PD_LINE;
        in.geometry.line.start = (PdPoint){3, 0};
        in.geometry.line.end = (PdPoint){4, 0};
        CHECK(h->pd_add_entity(t, &in, &tmp) == PD_OK);
        CHECK(h->pd_read_entity(d, tmp, &out) == PD_OK && out->geometry.line.start.x == 3);
        CHECK(h->pd_delete(t, tmp) == PD_OK);
        in.kind = PD_CIRCLE;
        in.geometry.circle.center = (PdPoint){0, 10};
        in.geometry.circle.radius = 5;
        CHECK(h->pd_add_entity(t, &in, &tmp) == PD_OK);
        CHECK(h->pd_read_entity(d, tmp, &out) == PD_OK && out->geometry.circle.radius == 5);
        CHECK(h->pd_delete(t, tmp) == PD_OK);
        p->arc = id;
    }
    return h->pd_commit(t);
}
static PdStatus PD_CALL init(const PdRegistrarV2* r, void** state) {
    *state = calloc(1, sizeof(Probe));
    if (!*state)
        return PD_NO_MEMORY;
#ifdef PD_FAIL_INIT
    (void)r->register_command(r->context, "GHOST", NULL, probe, *state);
    return PD_ERROR;
#else
    PdStatus s = r->register_command(r->context, "PROBE", "ПРОБА", probe, *state);
    if (s != PD_OK)
        return s;
    return r->register_command(r->context, "PROBE2", NULL, probe, *state);
#endif
}
static void PD_CALL stop(void* state) { free(state); }
static const PdPluginV2 api = {PD_ABI_V2, sizeof(PdPluginV2), init, stop};
PD_EXPORT const PdPluginV2* PD_CALL pd_plugin_query_v2(uint32_t v) { return v == PD_ABI_V2 ? &api : NULL; }
