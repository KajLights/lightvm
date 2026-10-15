/* Reference output of a restricted source compiler, written explicitly here.
 * The loader trusts this audited producer. This is NOT a native-code verifier.
 * No recursion, VLAs, allocation, syscalls, foreign calls or unbounded loops.
 * Future frontends must preserve these restrictions and inserted checks. */
#include "abi.h"
#ifndef NATIVE_REVISION
#define NATIVE_REVISION 1u
#endif
#if NATIVE_REVISION >= 2
#define NATIVE_SEED_XOR UINT32_C(0x13579bdf)
#else
#define NATIVE_SEED_XOR 0u
#endif

const NativeInfo *native_info(void) {
    static const NativeInfo info = {
        NATIVE_ABI, sizeof(NativeInfo), sizeof(NativeState), NATIVE_QUANTUM,
        NATIVE_STACK_LIMIT, 0, 0, NATIVE_REVISION
    };
    return &info;
}

int native_init(NativeState *state, uint32_t seed, uint32_t pixels, uint32_t mode) {
    if (!state || !pixels || pixels > 100000u || mode > 2u) return NATIVE_STATE;
    *state = (NativeState){ .seed = seed ^ NATIVE_SEED_XOR, .pixels = pixels, .mode = mode,
                            .init_remaining = 80u, .phase = NATIVE_NEW };
    return NATIVE_YIELD;
}

int native_init_step(NativeState *state, uint32_t budget) {
    if (!state || state->phase != NATIVE_NEW || !budget) return NATIVE_STATE;
    if (budget > NATIVE_QUANTUM) budget = NATIVE_QUANTUM;
    /* Representative preparation work with an explicit continuation. */
    while (budget-- && state->init_remaining) {
        state->x = state->x * 1664525u + 1013904223u;
        state->init_remaining--;
    }
    if (state->init_remaining) return NATIVE_YIELD;
    if (state->mode == 2u) return NATIVE_INIT_FAILED;
    state->phase = NATIVE_IDLE;
    return NATIVE_DONE;
}

int native_begin(NativeState *state, uint32_t frame) {
    if (!state || (state->phase != NATIVE_IDLE && state->phase != NATIVE_READY))
        return NATIVE_STATE;
    state->frame = frame;
    state->cursor = 0;
    state->x = state->seed ^ (frame * UINT32_C(0x9e3779b9));
    state->phase = NATIVE_RENDER;
    return NATIVE_YIELD;
}

int native_step(NativeState *state, const NativeView *view, uint32_t budget) {
    if (!state || state->phase != NATIVE_RENDER || !budget) return NATIVE_STATE;
    int valid = native_validate_view(view);
    if (valid < 0) return valid;
    if (view->bytes / 3u < state->pixels) return NATIVE_BOUNDS;
    if (budget > NATIVE_QUANTUM) budget = NATIVE_QUANTUM;
    while (budget--) {
        state->total_work++;
        if (state->mode == 1u) {
            /* A nonterminating SOURCE loop compiled with a fuel backedge. */
            state->x = state->x * 1664525u + 1013904223u;
            continue;
        }
        uint32_t i = state->cursor;
        if (i >= state->pixels || i >= view->bytes / 3u) return NATIVE_BOUNDS;
        uint32_t x = state->x;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        x += i + UINT32_C(0x6d2b79f5);
        size_t offset = (size_t)i * 3u;
        view->data[offset] = (uint8_t)x;
        view->data[offset + 1u] = (uint8_t)(x >> 8);
        view->data[offset + 2u] = (uint8_t)(x >> 16);
        state->x = x;
        state->cursor++;
        if (state->cursor == state->pixels) {
            state->phase = NATIVE_READY;
            return NATIVE_DONE;
        }
    }
    return NATIVE_YIELD;
}

int native_cancel(NativeState *state) {
    if (!state) return NATIVE_STATE;
    state->phase = NATIVE_CANCELLED;
    return NATIVE_DONE;
}
