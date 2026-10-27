#ifndef LIGHTVM_TRANSITION_H
#define LIGHTVM_TRANSITION_H
#include "composition.h"
typedef enum { TRANSITION_CROSSFADE, TRANSITION_WIPE, TRANSITION_SOFT_WIPE,
    TRANSITION_DISSOLVE, TRANSITION_DIP, TRANSITION_FLASH, TRANSITION_CUSTOM,
    TRANSITION_PROGRAM, TRANSITION_EFFECT_COUNT } TransitionEffect;
typedef enum { TRANSITION_MANUAL, TRANSITION_TIMED, TRANSITION_SOURCE, TRANSITION_INPUT } TransitionDriver;
typedef struct {
    VmInstance vm;
    uint8_t *baseline;
    uint64_t random_state;
    char name[OBJECT_NAME_LIMIT+1];
} TransitionProgram;
typedef struct {
    PixelBuffer *output, target;
    Composition *compositions[2];
    uint8_t *allocation, *working, *front;
    ContributionStamp sampled[2][COMPOSITION_LAYER_LIMIT],front_stamps[2][COMPOSITION_LAYER_LIMIT];
    unsigned pixel, phase, restore_offset, schedule_slot;
    uint32_t frames, composite_frames, sampled_frames[2], seed, assignment;
    uint64_t started, output_started, composite_us, output_us, next_frame, next_present;
    double progress, sampled_progress, start, target_progress, duration, interval;
    TransitionEffect effect;
    TransitionDriver driver;
    InputPort *input; InputRegistry *input_registry;
    char source[OBJECT_NAME_LIMIT+1], error[100];
    bool playing, composing, prepared, pending, dirty, ticking, command_active;
    /* Startup waits for the output scheduler, after native initialization. */
    bool start_pending;
    /* Completed preset recalls need only B. Interactive A/B keeps both live. */
    bool destination_only;
    /* Named transition in [0], or legacy reveal mask [0] and RGBA paint [1]. */
    TransitionProgram *programs[2];
} Transition;
const char *transition_effect_name(TransitionEffect);
bool transition_percentage(double);
Transition *transition_create(PixelBuffer *);
void transition_program_destroy(TransitionProgram *);
int transition_program_capture_step(TransitionProgram *,size_t *);
bool transition_program_capture(TransitionProgram *);
void transition_destroy(Transition *);
void transition_invalidate(Transition *);
void transition_update(Transition *, uint64_t now);
bool transition_set(Transition *, double);
bool transition_run(Transition *, double target, double duration_ms);
bool transition_run_interval(Transition *, double target, double duration_ms, double interval_ms);
bool transition_assign(Transition *, const char *source);
bool transition_source(Transition *, const char *source, uint32_t assignment, double);
/* Takes ownership of a validated, allocated progress binding. */
void transition_bind_port(Transition *,InputRegistry *,InputPort *);
void transition_hold(Transition *);
void transition_tick(Transition *, unsigned budget);
void transition_play(Transition *, bool);
const char *transition_frame(Transition *, uint32_t delta_ms);
#endif
