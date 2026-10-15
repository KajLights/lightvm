#ifndef LIGHTVM_NATIVE_EXPERIMENT_ABI_H
#define LIGHTVM_NATIVE_EXPERIMENT_ABI_H
#include <stddef.h>
#include <stdint.h>

#define NATIVE_ABI 1u
#define NATIVE_QUANTUM 64u
#define NATIVE_STACK_LIMIT 512u

enum { NATIVE_YIELD = 0, NATIVE_DONE = 1, NATIVE_BOUNDS = -1,
       NATIVE_STALE = -2, NATIVE_READONLY = -3, NATIVE_STATE = -4,
       NATIVE_INIT_FAILED = -5 };
enum { NATIVE_NEW, NATIVE_IDLE, NATIVE_RENDER, NATIVE_READY, NATIVE_CANCELLED };

/* Guard metadata outlives any view, including after storage has been reclaimed.
 * Single-worker prototype: these fields are deliberately not atomic. */
typedef struct { uint64_t generation; uint32_t writable, alive; } NativeGuard;
typedef struct {
    uint8_t *data;
    size_t bytes;
    uint64_t generation;
    NativeGuard *guard;
} NativeView;

/* An explicit continuation: no native stack survives a call to step(). */
typedef struct {
    uint32_t seed, frame, cursor, x, pixels, phase, mode, init_remaining;
    uint64_t total_work;
} NativeState;

typedef struct {
    uint32_t abi_version, descriptor_bytes, state_bytes, max_step_work;
    uint32_t stack_limit_bytes, recursion_allowed, heap_allowed, artifact_revision;
} NativeInfo;

typedef int (*NativeBegin)(NativeState *, uint32_t);
typedef int (*NativeStep)(NativeState *, const NativeView *, uint32_t);
typedef struct {
    uint64_t elapsed_ns, cpu_ns, maximum_step_ns, steps;
    uint32_t frames, checksum;
} NativeBenchmark;

static inline int native_validate_view(const NativeView *view) {
    if (!view || !view->guard || !view->guard->alive ||
        view->guard->generation != view->generation) return NATIVE_STALE;
    if (!view->guard->writable) return NATIVE_READONLY;
    if (!view->data || view->bytes % 3u) return NATIVE_BOUNDS;
    return NATIVE_DONE;
}
#endif
