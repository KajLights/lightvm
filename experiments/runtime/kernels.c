#include <stdint.h>

// Compile this exact source to both WASM and native code. The native version
// is an unsandboxed reference, not a WASM AOT implementation.
#ifdef __wasm__
#define KERNEL(name) __attribute__((export_name(#name))) uint32_t name
__attribute__((import_module("led"), import_name("pixel_count")))
uint32_t bench_count(void);
#else
#define KERNEL(name) uint32_t native_##name
uint32_t bench_count(void);
#endif

static volatile uint32_t words[256];

KERNEL(bench_integer)(uint32_t n) {
    uint32_t x=0x12345678u;
    for (uint32_t i=0;i<n;++i) {
        x^=x<<13; x^=x>>17; x^=x<<5;
        x+=i;
    }
    return x;
}

KERNEL(bench_float)(uint32_t n) {
    double x=0.25;
    for (uint32_t i=0;i<n;++i) x=x*1.0000001+0.0001;
    return (uint32_t)(x*1000000.0);
}

KERNEL(bench_memory)(uint32_t n) {
    for (uint32_t i=0;i<256;++i) words[i]=i*2654435761u;
    uint32_t x=7;
    for (uint32_t i=0;i<n;++i) {
        uint32_t j=x&255u;
        x=(words[j]^i)*1664525u+1013904223u;
        words[j]=x;
    }
    return x;
}

KERNEL(bench_import)(uint32_t n) {
    uint32_t x=0;
    for (uint32_t i=0;i<n;++i) x+=bench_count()^i;
    return x;
}
