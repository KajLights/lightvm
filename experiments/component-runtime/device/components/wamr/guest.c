/* Explicit continuation fixture for the common 200-pixel device test. */
#include <stdint.h>
#ifndef CR_SEED_XOR
#define CR_SEED_XOR 0u
#endif
#define EXPORT __attribute__((visibility("default")))
#define PIXELS 200u
static uint8_t rgb[PIXELS * 3] __attribute__((aligned(16)));
static uint32_t seed, cursor, random_state, initialized;
EXPORT uint32_t configure(uint32_t value) {
    seed=value ^ CR_SEED_XOR; cursor=PIXELS; initialized=0; return 1;
}
EXPORT uint32_t prepare(void) {
    for(unsigned n=0;n<64 && initialized<sizeof(rgb);n++) rgb[initialized++]=0;
    return initialized==sizeof(rgb);
}
EXPORT uint32_t pixels(void) { return (uint32_t)(uintptr_t)rgb; }
EXPORT uint32_t begin(uint32_t frame) {
    if(initialized!=sizeof(rgb)) return 0;
    cursor=0; random_state=seed ^ (frame * UINT32_C(0x9e3779b9)); return 1;
}
EXPORT uint32_t step(void) {
    for(unsigned n=0;n<32 && cursor<PIXELS;n++) {
        random_state ^= random_state<<13;
        random_state ^= random_state>>17;
        random_state ^= random_state<<5;
        random_state += cursor + UINT32_C(0x6d2b79f5);
        rgb[cursor*3]=random_state;
        rgb[cursor*3+1]=random_state>>8;
        rgb[cursor*3+2]=random_state>>16;
        cursor++;
    }
    return cursor==PIXELS;
}
