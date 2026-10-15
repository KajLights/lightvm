#define _POSIX_C_SOURCE 200809L
#include "abi.h"
#include <time.h>

static uint64_t nanoseconds(clockid_t clock) {
    struct timespec ts;
    if (clock_gettime(clock, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

uint32_t core_hash(const uint8_t *data, size_t bytes) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < bytes; i++) hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}

int core_fill(const NativeView *view, uint8_t r, uint8_t g, uint8_t b) {
    int valid = native_validate_view(view);
    if (valid < 0) return valid;
    /* Native services are also bounded: this experiment caps views at 300px. */
    if (view->bytes > 900u) return NATIVE_BOUNDS;
    for (size_t i = 0; i < view->bytes; i += 3u) {
        view->data[i] = r; view->data[i+1] = g; view->data[i+2] = b;
    }
    return NATIVE_DONE;
}

int core_dim(const NativeView *view) {
    int valid = native_validate_view(view);
    if (valid < 0) return valid;
    if (view->bytes > 900u) return NATIVE_BOUNDS;
    for (size_t i = 0; i < view->bytes; i++) view->data[i] >>= 1;
    return NATIVE_DONE;
}

/* Measures the component C kernel independently of Python dispatch. Each frame
 * still passes through begin/step, fuel, state and view checks. */
int core_benchmark(NativeBegin begin, NativeStep step, NativeState *state,
                   const NativeView *view, uint32_t frames, uint32_t quantum,
                   NativeBenchmark *out) {
    if (!begin || !step || !state || !view || !out || !frames || !quantum)
        return NATIVE_STATE;
    *out = (NativeBenchmark){0};
    uint64_t start = nanoseconds(CLOCK_MONOTONIC);
    uint64_t cpu_start = nanoseconds(CLOCK_PROCESS_CPUTIME_ID);
    for (uint32_t frame = 0; frame < frames; frame++) {
        if (begin(state, frame) != NATIVE_YIELD) return NATIVE_STATE;
        int status = NATIVE_YIELD;
        uint32_t frame_steps = 0;
        while (status == NATIVE_YIELD) {
            uint64_t step_start = nanoseconds(CLOCK_MONOTONIC);
            status = step(state, view, quantum);
            uint64_t elapsed = nanoseconds(CLOCK_MONOTONIC) - step_start;
            if (elapsed > out->maximum_step_ns) out->maximum_step_ns = elapsed;
            out->steps++;
            if (++frame_steps > 10000u) return NATIVE_STATE;
        }
        if (status != NATIVE_DONE) return status;
    }
    out->cpu_ns = nanoseconds(CLOCK_PROCESS_CPUTIME_ID) - cpu_start;
    out->elapsed_ns = nanoseconds(CLOCK_MONOTONIC) - start;
    out->frames = frames;
    out->checksum = core_hash(view->data, view->bytes);
    return NATIVE_DONE;
}
