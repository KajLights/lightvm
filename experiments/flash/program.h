#ifndef LIGHTVM_FLASH_PROGRAM_H
#define LIGHTVM_FLASH_PROGRAM_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { const uint8_t *bytes; size_t size; uintptr_t handle; unsigned slot; } ProgramMapping;
bool program_map(ProgramMapping *,const uint8_t *,size_t);
void program_unmap(ProgramMapping *);
#endif
