/* ESP32-C3 trusted native package loader. Never accepts arbitrary native code
 * as sandboxed code. This experiment needs MEMPROT_FEATURE disabled for EXEC RAM. */
#include "backend.h"
#include "../native/abi.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(CONFIG_IDF_TARGET_ESP32C3)
#error "The flat native device package is validated only for ESP32-C3"
#endif

typedef int (*Entry)(uint32_t, NativeState *, const NativeView *, uint32_t, uint32_t);
struct CrModule {
    uint32_t *code;
    Entry entry;
    uint32_t revision;
    uint32_t instances;
    bool unload_pending;
};
struct CrInstance {
    CrModule *module;
    NativeState state;
    NativeGuard guard;
    NativeView view;
    uint8_t *pixels;
    bool destroy_pending;
};

enum { PACKAGE_HEADER = 64, PACKAGE_MAX_PAYLOAD = 16384, DEVICE_PIXELS = 200 };

static uint32_t read32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint32_t crc32(const uint8_t *bytes, size_t size) {
    uint32_t value = UINT32_C(0xffffffff);
    for (size_t i = 0; i < size; i++) {
        value ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++)
            value = (value >> 1) ^ (UINT32_C(0xedb88320) & (0u - (value & 1u)));
    }
    return ~value;
}

static CrModule *fail(char *error, size_t capacity, const char *reason) {
    if (error && capacity) snprintf(error, capacity, "%s", reason);
    return NULL;
}

const char *cr_backend(void) { return "direct-native"; }

bool cr_runtime_init(void) {
#if defined(CONFIG_ESP_SYSTEM_MEMPROT_FEATURE) && CONFIG_ESP_SYSTEM_MEMPROT_FEATURE
    return false;
#else
    return heap_caps_get_largest_free_block(MALLOC_CAP_EXEC | MALLOC_CAP_32BIT) > 1024;
#endif
}

CrModule *cr_load(uint8_t *bytes, size_t size, char *error, size_t capacity) {
    if (!bytes || size < PACKAGE_HEADER) return fail(error, capacity, "short native package");
    uint32_t header[16];
    for (unsigned i = 0; i < 16; i++) header[i] = read32(bytes + i * 4);
    if (header[0] != UINT32_C(0x314e5243) || header[1] != 1u ||
        header[2] != UINT32_C(0x00c30001) || header[3] != PACKAGE_HEADER ||
        header[5] != 0u || (header[7] != 1u && header[7] != 2u) ||
        header[8] != sizeof(NativeState) || header[9] != 32u ||
        header[10] != 512u || header[11] != 1u ||
        header[12] || header[13] || header[14] || header[15])
        return fail(error, capacity, "native ABI/target/manifest mismatch");
    size_t payload_size = header[4];
    if (!payload_size || payload_size > PACKAGE_MAX_PAYLOAD || payload_size % 4u ||
        size != PACKAGE_HEADER + payload_size)
        return fail(error, capacity, "native payload length mismatch");
    if (crc32(bytes + PACKAGE_HEADER, payload_size) != header[6])
        return fail(error, capacity, "native payload CRC mismatch");
    CrModule *module = calloc(1, sizeof(*module));
    if (!module) return fail(error, capacity, "native module allocation failed");
    module->code = heap_caps_malloc(payload_size, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
    if (!module->code) {
        free(module);
        return fail(error, capacity, "native executable allocation failed (check MEMPROT)");
    }
    /* IRAM may only support aligned word data access; never use memcpy here. */
    volatile uint32_t *destination = module->code;
    for (size_t i = 0; i < payload_size / 4u; i++)
        destination[i] = read32(bytes + PACKAGE_HEADER + i * 4u);
    __asm__ volatile("fence.i" ::: "memory");
    module->entry = (Entry)(uintptr_t)module->code;
    module->revision = header[7];
    if (module->entry(5u, NULL, NULL, 0u, 0u) !=
        (int)(UINT32_C(0x4e430100) | module->revision)) {
        heap_caps_free(module->code);
        free(module);
        return fail(error, capacity, "native exported descriptor mismatch");
    }
    return module;
}

void cr_unload(CrModule *module) {
    if (!module) return;
    if (module->instances) {
        module->unload_pending = true;
        return;
    }
    heap_caps_free(module->code);
    free(module);
}

CrInstance *cr_create(CrModule *module, uint32_t seed, uint32_t pixels) {
    if (!module || module->unload_pending || pixels != DEVICE_PIXELS) return NULL;
    CrInstance *instance = calloc(1, sizeof(*instance));
    if (!instance) return NULL;
    instance->pixels = heap_caps_malloc((size_t)pixels * 3u,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    if (!instance->pixels) { free(instance); return NULL; }
    memset(instance->pixels, 0, (size_t)pixels * 3u);
    instance->module = module;
    instance->guard = (NativeGuard){1u, 1u, 1u};
    instance->view = (NativeView){instance->pixels, (size_t)pixels * 3u, 1u, &instance->guard};
    if (module->entry(0u, &instance->state, &instance->view, seed, pixels) != NATIVE_YIELD) {
        heap_caps_free(instance->pixels);
        free(instance);
        return NULL;
    }
    module->instances++;
    return instance;
}

int cr_prepare(CrInstance *instance) {
    if (!instance || instance->destroy_pending || !instance->guard.writable) return -1;
    int result = instance->module->entry(1u, &instance->state, &instance->view, 0u, 0u);
    return result < 0 ? -1 : result;
}

int cr_begin(CrInstance *instance, uint32_t frame) {
    if (!instance || instance->destroy_pending || !instance->guard.writable) return -1;
    int result = instance->module->entry(2u, &instance->state, &instance->view, frame, 0u);
    return result == NATIVE_YIELD ? 0 : -1;
}

int cr_step(CrInstance *instance) {
    if (!instance || instance->destroy_pending || !instance->guard.writable) return -1;
    int result = instance->module->entry(3u, &instance->state, &instance->view, 0u, 0u);
    return result < 0 ? -1 : result;
}

uint8_t *cr_pixels(CrInstance *instance) { return instance ? instance->pixels : NULL; }

void cr_seal(CrInstance *instance, bool sealed) {
    if (!instance || !instance->guard.alive) return;
    instance->guard.generation++;
    instance->guard.writable = sealed ? 0u : 1u;
    instance->view.generation = instance->guard.generation;
    if (!sealed && instance->destroy_pending) cr_destroy(instance);
}

void cr_destroy(CrInstance *instance) {
    if (!instance) return;
    if (!instance->guard.writable) {
        instance->destroy_pending = true;
        return;
    }
    CrModule *module = instance->module;
    module->entry(4u, &instance->state, &instance->view, 0u, 0u);
    instance->guard.alive = 0u;
    heap_caps_free(instance->pixels);
    free(instance);
    module->instances--;
    if (!module->instances && module->unload_pending) cr_unload(module);
}
