/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Both headers may coexist. A bad v2 must not be initialized, nor rescued by a working v1 export. */
#include <planedraft/pd_plugin_api.h>
#include <planedraft/plugin.h>
static int32_t PD_CALL ok(const PdHostV1* h) {
    (void)h;
    return PD_OK;
}
static const PdPluginV1 legacy = {PD_ABI_V1, sizeof(PdPluginV1), "NODOWNGRADE", ok};
PD_EXPORT const PdPluginV1* PD_CALL pd_plugin_query(uint32_t api) {
    return api == PD_ABI_V1 ? &legacy : NULL;
}
static PdStatus PD_CALL init(const PdRegistrarV2* r, void** state) {
    (void)r;
    (void)state;
    return PD_ERROR;
}
static void PD_CALL stop(void* state) { (void)state; }
#ifdef PD_WRONG_VERSION
static const PdPluginV2 bad = {PD_ABI_V2 + 1, sizeof(PdPluginV2), init, stop};
#else
static const PdPluginV2 bad = {PD_ABI_V2, offsetof(PdPluginV2, shutdown), init, stop};
#endif
PD_EXPORT const PdPluginV2* PD_CALL pd_plugin_query_v2(uint32_t api) {
    return api == PD_ABI_V2 ? &bad : NULL;
}
