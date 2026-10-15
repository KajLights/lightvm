/* Device adapter for the exact VM used by the host architecture experiment.
 * The uploaded package is borrowed, not staged on the stack or copied. */
#include "backend.h"

#define TYPED_VM_CORE_ONLY 1
#include "../typed/vm.c"

struct CrModule {
    Program *program;
    unsigned references;
    bool invalid;
};

struct CrInstance {
    CrModule *module;
    Instance *vm;
    bool prepared;
    bool destroy_pending;
};

/* This is an admission ceiling, not a reservation. ESP-IDF's allocator also
 * rejects requests that the actual heap cannot fulfill. Core heap measurements
 * include these allocations and the separately owned uploaded packages. */
static Memory device_memory = {.limit = 64u * 1024u, .fail_after = -1};
static Pool device_pool = {.mem = &device_memory};

const char *cr_backend(void) { return "typed-interpreter"; }

bool cr_runtime_init(void) { return true; }

CrModule *cr_load(uint8_t *bytes, size_t size, char *error, size_t capacity) {
    if (!bytes) {
        if (error && capacity) snprintf(error, capacity, "missing package");
        return NULL;
    }
    Program *program = load_begin(&device_memory, bytes, size);
    if (!program) {
        if (error && capacity) snprintf(error, capacity, "invalid header or allocation failure");
        return NULL;
    }
    CrModule *module = take(&device_memory, sizeof(*module));
    if (!module) {
        program_release(&device_memory, program);
        if (error && capacity) snprintf(error, capacity, "module allocation failed");
        return NULL;
    }
    module->program = program;
    module->references = 1;
    return module;
}

static void module_release(CrModule *module) {
    if (--module->references == 0) {
        program_release(&device_memory, module->program);
        give(&device_memory, module, sizeof(*module));
    }
}

void cr_unload(CrModule *module) {
    if (module) module_release(module);
}

CrInstance *cr_create(CrModule *module, uint32_t seed, uint32_t pixels) {
    if (!module || module->invalid || !pixels || pixels > 200) return NULL;
    CrInstance *instance = take(&device_memory, sizeof(*instance));
    if (!instance) return NULL;
    instance->vm = instance_new(&device_pool, module->program, seed, pixels);
    if (!instance->vm) {
        give(&device_memory, instance, sizeof(*instance));
        return NULL;
    }
    instance->module = module;
    module->references++;
    return instance;
}

static int step_result(int status) {
    if (status == DONE) return 1;
    if (status == READY || status == YIELDED) return 0;
    return -1;
}

int cr_prepare(CrInstance *instance) {
    if (!instance || instance->destroy_pending || instance->module->invalid) return -1;
    if (instance->prepared) return 1;
    if (instance->vm->owned->locks) return -1;
    Program *program = instance->module->program;
    if (program->validated < program->count) {
        int validation = load_step(program, 4);
        if (validation < 0) {
            instance->module->invalid = true;
            return -1;
        }
        /* Finish validation in this call; initialize on the next call. */
        return 0;
    }
    int result = step_result(resume(instance->vm, QUANTUM));
    if (result == 1) instance->prepared = true;
    return result;
}

int cr_begin(CrInstance *instance, uint32_t frame) {
    if (!instance || !instance->prepared || instance->destroy_pending ||
        instance->module->invalid || instance->vm->owned->locks ||
        instance->vm->status != DONE) return -1;
    frame_start(instance->vm, frame);
    return 0;
}

int cr_step(CrInstance *instance) {
    if (!instance || !instance->prepared || instance->destroy_pending ||
        instance->module->invalid || instance->vm->owned->locks) return -1;
    return step_result(resume(instance->vm, QUANTUM));
}

uint8_t *cr_pixels(CrInstance *instance) {
    return instance ? instance->vm->owned->data : NULL;
}

void cr_destroy(CrInstance *instance) {
    if (!instance) return;
    if (instance->vm->owned->locks) {
        /* Never reclaim a peripheral's backing allocation while sealed. */
        instance->destroy_pending = true;
        return;
    }
    CrModule *module = instance->module;
    instance_free(instance->vm);
    give(&device_memory, instance, sizeof(*instance));
    module_release(module);
}

void cr_seal(CrInstance *instance, bool sealed) {
    if (!instance) return;
    instance->vm->owned->locks = sealed ? 1 : 0;
    if (!sealed && instance->destroy_pending) cr_destroy(instance);
}
