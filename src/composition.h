#ifndef LIGHTVM_COMPOSITION_H
#define LIGHTVM_COMPOSITION_H
#include "vm.h"
#define COMPOSITION_LAYER_LIMIT 8u
typedef struct {
    char id[OBJECT_NAME_LIMIT+1], program[OBJECT_NAME_LIMIT+1];
    uint32_t crc, sampled_frame;
    ContributionStamp sampled,front_stamp;
    uint8_t opacity, blend;
    PixelBuffer target;
    VmInstance vm;
} CompositionLayer;
typedef struct Composition {
    struct InputSourceScope *sources;
    char preset[OBJECT_NAME_LIMIT+1];
    uint32_t preset_crc;
    PixelBuffer *output;
    uint8_t *allocation, *working, *front;
    unsigned count, layer_count, cursor, pixel;
    uint32_t revision, frames, composite_frames;
    uint64_t next_frame, next_present, output_started, composite_us, output_us;
    bool playing, composing, prepared, pending, dirty, command_active, ticking, front_pinned;
    char error[100];
    CompositionLayer layers[];
} Composition;
Composition *composition_create(PixelBuffer *, unsigned layers);
void composition_destroy(Composition *);
void composition_offscreen(Composition *, PixelBuffer *);
void composition_install_prepared(Composition *, PixelBuffer *);
void composition_publish(Composition *);
void composition_presented(Composition *,const ContributionStamp *,uint64_t);
CompositionLayer *composition_layer(Composition *, const char *id);
void composition_invalidate(Composition *);
// Collector phase only: checkpoints are deferred while publishing/preparing.
void composition_collect_ready(Composition *);
void composition_tick(Composition *, unsigned budget);
bool composition_prepare_step(Composition *);
void composition_prepare(Composition *);
void composition_play(Composition *, bool playing);
const char *composition_frame(Composition *, uint32_t delta_ms);
void composition_remove_layer(Composition *, unsigned index);
#endif
