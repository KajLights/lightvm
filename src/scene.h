#ifndef LIGHTVM_SCENE_H
#define LIGHTVM_SCENE_H
#include "vm.h"
#define SCENE_LAYER_LIMIT 8u
typedef struct {
    char id[OBJECT_NAME_LIMIT+1], procedure[OBJECT_NAME_LIMIT+1];
    uint32_t crc, sampled_frame;
    uint8_t opacity;
    PixelBuffer target;
    VmInstance vm;
} SceneLayer;
typedef struct Scene {
    PixelBuffer *output;
    uint8_t *allocation, *working, *front;
    unsigned count, layer_count, cursor, pixel;
    uint32_t revision, frames, composite_frames;
    uint64_t next_frame, output_started, composite_us, output_us;
    bool playing, composing, prepared, pending, dirty, command_active, ticking;
    char error[100];
    SceneLayer layers[];
} Scene;
Scene *scene_create(PixelBuffer *, unsigned layers);
void scene_destroy(Scene *);
SceneLayer *scene_layer(Scene *, const char *id);
void scene_invalidate(Scene *);
void scene_tick(Scene *, unsigned budget);
void scene_prepare(Scene *);
void scene_play(Scene *, bool playing);
const char *scene_frame(Scene *, uint32_t delta_ms);
void scene_remove_layer(Scene *, unsigned index);
#endif
