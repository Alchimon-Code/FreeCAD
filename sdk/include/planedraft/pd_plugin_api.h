/* Copyright (c) 2026 PlaneDraft contributors
 * SPDX-License-Identifier: MIT */
#ifndef PLANEDRAFT_PD_PLUGIN_API_H
#define PLANEDRAFT_PD_PLUGIN_API_H
#include <stddef.h>
#include <stdint.h>

#ifndef PD_CALL
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
#endif
#ifdef __cplusplus
extern "C" {
#endif
/** @file pd_plugin_api.h
 * Native SDK v2. C11, default packing, same process architecture (Windows x64 / Linux x86-64).
 * V1's symbol and layouts in plugin.h are independent and frozen. Check api_version and struct_size
 * before accessing a table. V2 required prefixes below are frozen; future fields may only be appended.
 * All calls are synchronous on the invoking thread. Never throw, retain a borrowed pointer, or free
 * memory from the other runtime. Strings are NUL-terminated UTF-8, coordinates binary64, angles radians.
 */
#define PD_ABI_V2 2u
typedef int32_t PdStatus;
#ifndef PD_OK
#define PD_OK 0
#define PD_ERROR 1
#endif
#define PD_INVALID_ARGUMENT 2
#define PD_NOT_FOUND 3
#define PD_BAD_STATE 4
#define PD_UNSUPPORTED 5
#define PD_NO_MEMORY 6
#define PD_CONFLICT 7

typedef uint64_t PdId; /* object identity, not an address; layer 0 exists in every document */
typedef struct PdDocumentHandle {
    uint64_t opaque;
} PdDocumentHandle;
typedef struct PdTransactionHandle {
    uint64_t opaque;
} PdTransactionHandle;
typedef struct PdPoint {
    double x, y;
} PdPoint;
typedef struct PdBox {
    PdPoint min, max;
} PdBox;
typedef struct PdMatrix {
    double a, b, tx, c, d, ty;
} PdMatrix; /* row-major affine 2x3 */
typedef struct PdRgb {
    uint8_t r, g, b;
} PdRgb;
typedef struct PdVertex {
    PdPoint point;
    double bulge;
} PdVertex;
typedef struct PdIds {
    const PdId* data;
    uint64_t count;
} PdIds;
#define PD_LINE 1u
#define PD_CIRCLE 2u
#define PD_ARC 3u
#define PD_POLYLINE 4u
#define PD_TEXT 5u
#define PD_BY_LAYER 0u
#define PD_BY_BLOCK 1u
#define PD_EXPLICIT 2u
#define PD_HIDDEN 1u
#define PD_SELECTED 2u
#define PD_FROZEN 2u /* layer flags */
#define PD_LOCKED 4u

typedef struct PdEntityData {
    uint32_t struct_size, kind;
    PdId layer;
    uint32_t flags, color_mode, linetype_mode, weight_mode;
    PdRgb color;
    const char* linetype;
    double weight;
    union {
        struct {
            PdPoint start, end;
        } line;
        struct {
            PdPoint center;
            double radius;
        } circle;
        struct {
            PdPoint center;
            double radius, start, sweep;
        } arc;
        struct {
            const PdVertex* vertices;
            uint64_t count;
            uint32_t closed;
        } polyline;
        struct {
            const char* utf8;
            PdBox local_ink;
            PdMatrix placement;
        } text;
    } geometry;
} PdEntityData;
typedef struct PdLayerData {
    uint32_t struct_size, flags;
    const char* name;
    PdRgb color;
    const char* linetype;
    double weight;
} PdLayerData;

typedef struct PdHostV2 {
    uint32_t api_version, struct_size;
    PdDocumentHandle document; /* valid during this command only */
    /** At most one edit group per command. commit seals it. Publication occurs only when run returns PD_OK.
     * A failed write poisons the group. An unclosed group or failed command publishes nothing. */
    PdStatus(PD_CALL* pd_begin_transaction)(PdDocumentHandle, PdTransactionHandle*);
    PdStatus(PD_CALL* pd_commit)(PdTransactionHandle);
    PdStatus(PD_CALL* pd_rollback)(PdTransactionHandle);
    PdStatus(PD_CALL* pd_add_entity)(PdTransactionHandle, const PdEntityData*, PdId*);
    PdStatus(PD_CALL* pd_add_layer)(PdTransactionHandle, const PdLayerData*, PdId*);
    PdStatus(PD_CALL* pd_transform)(PdTransactionHandle, PdId, PdMatrix);
    PdStatus(PD_CALL* pd_delete)(PdTransactionHandle, PdId);
    /** Reads by ID see prior staged writes. All returned snapshots are host-owned copies. */
    PdStatus(PD_CALL* pd_read_entity)(PdDocumentHandle, PdId, const PdEntityData**);
    PdStatus(PD_CALL* pd_read_layer)(PdDocumentHandle, PdId, const PdLayerData**);
    /** Enumeration/spatial queries read the committed document. Selection reads the current editor IDs.
     * query_box: inclusive AABB candidates, visible_only = 0 or 1. */
    PdStatus(PD_CALL* pd_entities)(PdDocumentHandle, PdIds*);
    PdStatus(PD_CALL* pd_layers)(PdDocumentHandle, PdIds*);
    PdStatus(PD_CALL* pd_selection)(PdDocumentHandle, PdIds*);
    PdStatus(PD_CALL* pd_query_box)(PdDocumentHandle, PdBox, uint32_t visible_only, PdIds*);
    /** Release root snapshot pointer or PdIds.data, NOT internal fields. NULL is a no-op.
     * All outstanding buffers expire and are reclaimed when the command returns. */
    PdStatus(PD_CALL* pd_free_buffer)(PdDocumentHandle, const void*);
    PdStatus(PD_CALL* pd_log)(PdDocumentHandle, uint32_t is_stderr, const char* utf8);
} PdHostV2;

/** arguments_utf8 is the borrowed command tail (no command name); never NULL. */
typedef PdStatus(PD_CALL* PdRunV2)(void* user, const PdHostV2*, const char* arguments_utf8);
typedef struct PdRegistrarV2 {
    uint32_t api_version, struct_size;
    void* context;
    /** Init only. Global uppercase ASCII, optional uppercase RU alias (NULL = none).
     * Names are copied, user and run must remain valid through shutdown. */
    PdStatus(PD_CALL* register_command)(void* context, const char* global, const char* ru, PdRunV2 run,
                                        void* user);
} PdRegistrarV2;
typedef struct PdPluginV2 {
    uint32_t api_version, struct_size;
    PdStatus(PD_CALL* init)(const PdRegistrarV2*, void** instance);
    /** Called once after each attempted init, even on failure (instance initially NULL).
     * Stop all work/release plugin-owned resources before returning. No host calls here. */
    void(PD_CALL* shutdown)(void* instance);
} PdPluginV2;
/* Test a field/prefix, not sizeof(a future, extended header). Geometry union layout is frozen. */
#define PD_FIELD_END(type, field) (offsetof(type, field) + sizeof(((type*)0)->field))
#define PD_HOST_V2_MIN_SIZE PD_FIELD_END(PdHostV2, pd_log)
#define PD_REGISTRAR_V2_MIN_SIZE PD_FIELD_END(PdRegistrarV2, register_command)
#define PD_PLUGIN_V2_MIN_SIZE PD_FIELD_END(PdPluginV2, shutdown)
#define PD_ENTITY_V2_MIN_SIZE PD_FIELD_END(PdEntityData, geometry)
#define PD_LAYER_V2_MIN_SIZE PD_FIELD_END(PdLayerData, weight)
typedef const PdPluginV2*(PD_CALL* PdQueryV2Fn)(uint32_t host_api);
PD_EXPORT const PdPluginV2* PD_CALL pd_plugin_query_v2(uint32_t host_api);
#ifdef __cplusplus
}
#endif
#endif
