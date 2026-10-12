#ifndef LIGHTVM_CONFIG_H
#define LIGHTVM_CONFIG_H

// A port supplies a board/resource profile; defaults are the portable baseline.
#ifdef LIGHTVM_BOARD_CONFIG
#include LIGHTVM_BOARD_CONFIG
#endif
#ifndef PIXEL_COUNT
#define PIXEL_COUNT 100u
#endif
#ifndef MODULE_LIMIT
#define MODULE_LIMIT (32u * 1024u)
#endif
#ifndef VM_HEAP_LIMIT
#define VM_HEAP_LIMIT (128u * 1024u)
#endif
#ifndef VM_STACK_SIZE
#define VM_STACK_SIZE 8192u
#endif
#ifndef OUTPUT_LIMIT
#define OUTPUT_LIMIT (16u * 1024u)
#endif
#ifndef RUN_TIMEOUT_US
#define RUN_TIMEOUT_US 500000u
#endif
#ifndef FRAME_INTERVAL_US
#define FRAME_INTERVAL_US 16667u
#endif
// The interpreter and guest API currently support at most one WASM page.
#define LINEAR_MEMORY_LIMIT 65536u
#define LIGHTVM_ANIMATION_API_VERSION 1u

#if PIXEL_COUNT < 1 || PIXEL_COUNT > 65535
#error "PIXEL_COUNT must be 1..65535"
#endif
#if MODULE_LIMIT < 8 || MODULE_LIMIT > 2147483647
#error "MODULE_LIMIT must fit a positive protocol i32 result"
#endif
#if VM_HEAP_LIMIT < 1 || VM_STACK_SIZE < 1 || OUTPUT_LIMIT < 1
#error "Heap, stack and output budgets must be positive"
#endif
#if RUN_TIMEOUT_US < 1 || FRAME_INTERVAL_US < 1
#error "Execution deadline and frame interval must be positive"
#endif
#endif
