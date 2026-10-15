#ifndef COMPONENT_DEVICE_BACKEND_H
#define COMPONENT_DEVICE_BACKEND_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Common, single-worker device contract. The package bytes remain owned and
 * stable in the core until cr_unload(). RGB storage is retained with its instance
 * until the real peripheral completion. No backend may write a sealed buffer. */
typedef struct CrModule CrModule;
typedef struct CrInstance CrInstance;
const char *cr_backend(void);
bool cr_runtime_init(void);
CrModule *cr_load(uint8_t *bytes, size_t size, char *error, size_t capacity);
void cr_unload(CrModule *module);
CrInstance *cr_create(CrModule *module, uint32_t seed, uint32_t pixels);
/* prepare/step: -1 error, 0 yielded, 1 complete. begin: 0 success, -1 error. */
int cr_prepare(CrInstance *instance);
int cr_begin(CrInstance *instance, uint32_t frame);
int cr_step(CrInstance *instance);
uint8_t *cr_pixels(CrInstance *instance);
void cr_seal(CrInstance *instance, bool sealed);
void cr_destroy(CrInstance *instance);
#endif
