/* Copyright (c) 2026 PlaneDraft contributors
 * SPDX-License-Identifier: MIT */
#ifndef PLANEDRAFT_PLUGIN_H
#define PLANEDRAFT_PLUGIN_H

#include <stdint.h>

#if defined(_WIN32)
#define PD_CALL __cdecl
#if defined(PD_BUILD_PLUGIN)
#define PD_EXPORT __declspec(dllexport)
#else
#define PD_EXPORT
#endif
#else
#define PD_CALL
#define PD_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @file plugin.h
 *  @brief PlaneDraft plugin ABI v1 (Windows x64).
 *  Match host architecture and default struct packing.
 *  All calls are synchronous on the document thread. No exceptions across this boundary.
 *  Never free memory allocated by the other side. Append fields only, never reorder.
 */
#define PD_ABI_V1 1u
#define PD_OK 0
#define PD_ERROR 1

/** Borrowed only for the duration of run(). Do not retain the table or context. */
typedef struct PdHostV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void* context;
    /** One undoable edit. Invalid or coincident endpoints return PD_ERROR. */
    int32_t(PD_CALL* add_line)(void* context, double x1, double y1, double x2, double y2);
} PdHostV1;

/** Plugin-owned immutable storage, valid until the module is unloaded. */
typedef struct PdPluginV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* command; /**< Unique NUL-terminated ASCII name: [A-Z][A-Z0-9_]{0,31}. */
    int32_t(PD_CALL* run)(const PdHostV1* host);
} PdPluginV1;

typedef const PdPluginV1*(PD_CALL* PdQueryFn)(uint32_t host_abi);
/** Only exported symbol required by v1. Return NULL if host_abi is unsupported. */
PD_EXPORT const PdPluginV1* PD_CALL pd_plugin_query(uint32_t host_abi);

#ifdef __cplusplus
}
#endif
#endif
