#ifndef LIGHTVM_CONFIG_H
#define LIGHTVM_CONFIG_H

#ifndef LIGHTVM_WIFI
#define LIGHTVM_WIFI 0
#endif
#ifndef LIGHTVM_HTTP
#define LIGHTVM_HTTP 0
#endif
#ifndef LIGHTVM_WEBSOCKET
#define LIGHTVM_WEBSOCKET 0
#endif
#if LIGHTVM_WEBSOCKET && !LIGHTVM_HTTP
#error "WebSocket management requires HTTP"
#endif
#ifndef LIGHTVM_BOOT_SEQUENCE
#define LIGHTVM_BOOT_SEQUENCE 0
#endif
// The detector remains useful by itself; AP recovery needs all three services.
#define LIGHTVM_POWER_RECOVERY (LIGHTVM_BOOT_SEQUENCE && LIGHTVM_WIFI && LIGHTVM_HTTP)
#if LIGHTVM_BOOT_SEQUENCE
#define BOOT_SEQUENCE_STORAGE_BYTES 8192u
#endif

// A port supplies a board/resource profile; defaults are the portable baseline.
#ifdef LIGHTVM_BOARD_CONFIG
#include LIGHTVM_BOARD_CONFIG
#endif
#ifndef PIXEL_CAPACITY
#define PIXEL_CAPACITY 1024u
#endif
#define CONFIG_LIMIT 8192u
#define OBJECT_NAME_LIMIT 32u
#ifndef STORAGE_BYTES
#define STORAGE_BYTES (512u * 1024u)
#endif
#if STORAGE_BYTES < 16384 || STORAGE_BYTES % 8192 != 0
#error "STORAGE_BYTES must be at least 16 KiB and a multiple of 8 KiB"
#endif
#ifndef MODULE_LIMIT
#define MODULE_LIMIT (32u * 1024u)
#endif
#ifndef VM_HEAP_LIMIT
#define VM_HEAP_LIMIT (128u * 1024u)
#endif
#ifndef FRAME_INTERVAL_US
#define FRAME_INTERVAL_US 16667u
#endif

#if PIXEL_CAPACITY < 1 || PIXEL_CAPACITY > 65535
#error "PIXEL_CAPACITY must be 1..65535"
#endif
#if MODULE_LIMIT < 8 || MODULE_LIMIT > 2147483647
#error "MODULE_LIMIT must fit a positive protocol i32 result"
#endif
#if VM_HEAP_LIMIT < 1
#error "Heap budget must be positive"
#endif
#if FRAME_INTERVAL_US < 1
#error "Frame interval must be positive"
#endif
#endif
