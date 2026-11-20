/* Copyright (c) 2026 PlaneDraft contributors. SPDX-License-Identifier: MIT */
#include <planedraft/pd_plugin_api.h>
#include <string.h>

/* _FRAME [width] -- C only; no Qt, core, C++ ABI or imported host functions. */
static PdStatus PD_CALL frame(void* user, const PdHostV2* h, const char* args) {
    double w = 80.0;
    PdTransactionHandle tx;
    PdLayerData layer = {0};
    PdEntityData e = {0};
    PdVertex v[4];
    PdId lid, id;
    PdStatus s;
    unsigned i;
    (void)user;
    if (!h || h->api_version != PD_ABI_V2 || h->struct_size < PD_HOST_V2_MIN_SIZE)
        return PD_UNSUPPORTED;
    if (*args) {
        /* A decimal width always uses '.', regardless of the process locale. */
        w = 0;
        while (*args >= '0' && *args <= '9')
            w = w * 10 + (*args++ - '0');
        if (*args == '.') {
            ++args;
            double place = .1;
            while (*args >= '0' && *args <= '9') {
                w += place * (*args++ - '0');
                place *= .1;
            }
        }
        if (*args || !(w >= 10 && w <= 1000000)) {
            h->pd_log(h->document, 1, "Use: _FRAME [decimal width, 10..1000000]");
            return PD_INVALID_ARGUMENT;
        }
    }
    layer.struct_size = sizeof layer;
    layer.name = "Frame";
    layer.linetype = "Continuous";
    layer.weight = .25;
    layer.color.r = 86;
    layer.color.g = 202;
    layer.color.b = 220;
    s = h->pd_begin_transaction(h->document, &tx);
    if (s != PD_OK)
        return s;
    /* Reuse the layer on subsequent invocations. Snapshots are released by the allocating host. */
    {
        PdIds layers = {0};
        int found = 0;
        s = h->pd_layers(h->document, &layers);
        if (s != PD_OK)
            goto fail;
        for (uint64_t k = 0; k < layers.count; ++k) {
            const PdLayerData* l = NULL;
            s = h->pd_read_layer(h->document, layers.data[k], &l);
            if (s != PD_OK)
                goto fail;
            if (l->struct_size < PD_LAYER_V2_MIN_SIZE) {
                s = PD_UNSUPPORTED;
                goto fail;
            }
            if (strcmp(l->name, layer.name) == 0) {
                lid = layers.data[k];
                found = 1;
            }
            h->pd_free_buffer(h->document, l);
            if (found)
                break;
        }
        h->pd_free_buffer(h->document, layers.data);
        if (!found) {
            s = h->pd_add_layer(tx, &layer, &lid);
            if (s != PD_OK)
                goto fail;
        }
    }
    v[0] = (PdVertex){{0, 0}, 0};
    v[1] = (PdVertex){{w, 0}, 0};
    v[2] = (PdVertex){{w, w * .5}, 0};
    v[3] = (PdVertex){{0, w * .5}, 0};
    e.struct_size = sizeof e;
    e.kind = PD_POLYLINE;
    e.layer = lid;
    e.geometry.polyline.vertices = v;
    e.geometry.polyline.count = 4;
    e.geometry.polyline.closed = 1;
    s = h->pd_add_entity(tx, &e, &id);
    if (s != PD_OK)
        goto fail;
    for (i = 0; i < 4; ++i) {
        e.kind = PD_CIRCLE;
        e.geometry.circle.center = (PdPoint){(i & 1) ? w - 2 : 2, (i & 2) ? w * .5 - 2 : 2};
        e.geometry.circle.radius = 1;
        s = h->pd_add_entity(tx, &e, &id);
        if (s != PD_OK)
            goto fail;
    }
    e.kind = PD_TEXT;
    e.geometry.text.utf8 = "FRAME";
    e.geometry.text.local_ink = (PdBox){{0, 0}, {8, 2}};
    e.geometry.text.placement = (PdMatrix){1, 0, 3, 0, 1, 3};
    s = h->pd_add_entity(tx, &e, &id);
    if (s != PD_OK)
        goto fail;
    s = h->pd_log(h->document, 0, "FRAME: outline, four holes and label");
    if (s != PD_OK)
        goto fail;
    return h->pd_commit(tx);
fail:
    h->pd_rollback(tx);
    return s;
}
static PdStatus PD_CALL init(const PdRegistrarV2* r, void** instance) {
    (void)instance;
    if (!r || r->api_version != PD_ABI_V2 || r->struct_size < PD_REGISTRAR_V2_MIN_SIZE)
        return PD_UNSUPPORTED;
    return r->register_command(r->context, "FRAME", "РАМКА", frame, NULL);
}
static void PD_CALL shutdown_frame(void* instance) { (void)instance; }
static const PdPluginV2 descriptor = {PD_ABI_V2, sizeof(PdPluginV2), init, shutdown_frame};
PD_EXPORT const PdPluginV2* PD_CALL pd_plugin_query_v2(uint32_t version) {
    return version == PD_ABI_V2 ? &descriptor : NULL;
}
