// Copyright (c) 2026 PlaneDraft contributors
// SPDX-License-Identifier: MIT
#include <planedraft/plugin.h>

namespace {
int32_t PD_CALL run(const PdHostV1* host) {
    if (!host || host->abi_version != PD_ABI_V1 || host->struct_size < sizeof(PdHostV1) || !host->add_line) {
        return PD_ERROR;
    }
    return host->add_line(host->context, -60, -25, 60, 25);
}
constexpr PdPluginV1 plugin{PD_ABI_V1, sizeof(PdPluginV1), "SAMPLELINE", run};
} // namespace

extern "C" PD_EXPORT const PdPluginV1* PD_CALL pd_plugin_query(uint32_t host_abi) {
    return host_abi == PD_ABI_V1 ? &plugin : nullptr;
}
