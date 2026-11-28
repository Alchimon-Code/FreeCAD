/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <planedraft/plugin.h>
#include <stddef.h>

static int32_t PD_CALL never_run(const PdHostV1* host) {
    (void)host;
    return PD_ERROR;
}

/* Only struct_size is invalid: the host must reject it before reading run. */
static const PdPluginV1 short_table = {PD_ABI_V1, offsetof(PdPluginV1, run), "SHORT", never_run};
PD_EXPORT const PdPluginV1* PD_CALL pd_plugin_query(uint32_t host_abi) {
    return host_abi == PD_ABI_V1 ? &short_table : NULL;
}
