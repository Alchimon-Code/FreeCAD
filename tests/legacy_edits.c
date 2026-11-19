/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <planedraft/plugin.h>
#include <stddef.h>
/* Built solely against the original header. No v2 runtime or link dependency. */
static int32_t PD_CALL draw(const PdHostV1* h) {
    if (h->add_line(h->context, 1, 1, 1, 1) != PD_ERROR || h->add_line(h->context, 0, 0, 9, 9) != PD_OK)
        return PD_ERROR;
    return h->add_line(h->context, 3, 4, 5, 6);
}
static const PdPluginV1 api = {PD_ABI_V1, sizeof(PdPluginV1), "OLDEDITS", draw};
PD_EXPORT const PdPluginV1* PD_CALL pd_plugin_query(uint32_t v) { return v == PD_ABI_V1 ? &api : NULL; }
