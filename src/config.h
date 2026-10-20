#ifndef LIGHTVM_CONFIG_H
#define LIGHTVM_CONFIG_H

#ifndef LIGHTVM_WIFI
#define LIGHTVM_WIFI 0
#endif
#ifndef LIGHTVM_MQTT
#define LIGHTVM_MQTT 0
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
#ifdef LIGHTVM_VM_HEAP_BYTES
#undef VM_HEAP_LIMIT
#define VM_HEAP_LIMIT LIGHTVM_VM_HEAP_BYTES
#endif
#ifdef LIGHTVM_MODULE_BYTES
#undef MODULE_LIMIT
#define MODULE_LIMIT LIGHTVM_MODULE_BYTES
#endif
#ifdef LIGHTVM_PIXEL_CAPACITY
#undef PIXEL_CAPACITY
#define PIXEL_CAPACITY LIGHTVM_PIXEL_CAPACITY
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
#ifndef LIGHTVM_NATIVE_HEADROOM_BYTES
#define LIGHTVM_NATIVE_HEADROOM_BYTES 16384u
#endif
#ifndef LIGHTVM_PICO2_DCP_ENABLED
#define LIGHTVM_PICO2_DCP_ENABLED 0
#endif
#ifndef LIGHTVM_HTTP_CLIENTS
#define LIGHTVM_HTTP_CLIENTS 4u
#endif
#ifndef LIGHTVM_MAX_REFRESH_HZ
#define LIGHTVM_MAX_REFRESH_HZ 0u
#endif
#if LIGHTVM_MAX_REFRESH_HZ
#define FRAME_INTERVAL_US ((1000000u+LIGHTVM_MAX_REFRESH_HZ-1u)/LIGHTVM_MAX_REFRESH_HZ)
#else
#define FRAME_INTERVAL_US 0u
#endif
#ifndef LIGHTVM_RENDER_WORKERS
#define LIGHTVM_RENDER_WORKERS 2u
#endif
#ifndef LIGHTVM_RENDER_SLICE_US
#define LIGHTVM_RENDER_SLICE_US 500u
#endif
#ifndef LIGHTVM_INVOCATION_LIMIT
#define LIGHTVM_INVOCATION_LIMIT 10000000u
#endif
#ifndef LIGHTVM_BYTECODE_CACHE_BYTES
#define LIGHTVM_BYTECODE_CACHE_BYTES 256u
#endif
#if LIGHTVM_BYTECODE_CACHE_BYTES < 64 || LIGHTVM_BYTECODE_CACHE_BYTES > 16384 || (LIGHTVM_BYTECODE_CACHE_BYTES & (LIGHTVM_BYTECODE_CACHE_BYTES-1))
#error "Bytecode cache must be a power of two from 64 to 16384 bytes"
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
#if LIGHTVM_MAX_REFRESH_HZ > 1000
#error "Refresh cap must be 0..1000 Hz"
#endif
#if LIGHTVM_RENDER_WORKERS < 1 || LIGHTVM_RENDER_WORKERS > 2
#error "Render workers must be 1 or 2"
#endif
#if LIGHTVM_HTTP_CLIENTS < 1 || LIGHTVM_HTTP_CLIENTS > 8
#error "HTTP clients must be 1..8"
#endif
#endif
