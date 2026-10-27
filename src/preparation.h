#ifndef LIGHTVM_PREPARATION_H
#define LIGHTVM_PREPARATION_H
#include "command.h"
#include "composition.h"
#include "transition.h"
struct App;
struct AppOutput;
typedef enum { PREPARE_DIRECT,PREPARE_SLOT,PREPARE_COMPOSITION,PREPARE_PRESET,PREPARE_RECALL,PREPARE_RESET,PREPARE_LAYER,PREPARE_TRANSITION,PREPARE_PRESET_SAVE,PREPARE_CONFIG_VALIDATE,PREPARE_CONFIG_APPLY,PREPARE_CONFIG_SAVE } PreparationKind;
typedef struct {
    PreparationKind kind;
    unsigned slot;
    bool play,on,light_attached,boot;
    LightControls light;
    uint32_t revision,light_revision,generation,seed,catalog_revision,active_revision,saved_revision;
    TransitionEffect effect;
    char layer[OBJECT_NAME_LIMIT+1];
    const VmInstance *reset_from; /* borrowed only during submit */
    StoreEntry entry;
    char name[OBJECT_NAME_LIMIT+1];
} PreparationRequest;
typedef struct {
    PreparationRequest request;
    struct InputSourceScope *sources;
    bool source_demand;
    VmInstance *direct;
    PixelBuffer target;
    Composition *composition;
    TransitionProgram *transition,*paint;
    TransitionSpec transition_spec;
    const void *document;size_t document_size;
    Transition *presenter;Composition *wrapper;
    bool persisted;
} PreparedScene;
/* Admission copies all caller-owned values; no transport/parser pointers survive. */
const char *preparation_submit(struct App *,struct AppOutput *,const PreparationRequest *,const void *,size_t,uint32_t *);
void preparation_tick(struct App *);
void preparation_abort_all(struct App *);
size_t preparation_retained_bytes(const struct App *);
void preparation_destroy(struct App *);
void preparation_cancel_output(struct App *,const char *output);
bool preparation_manage(struct App *,uint16_t,const Json *,const char *);
bool preparation_source_result(struct App *,uint64_t,uint32_t *,const char **);
bool preparation_result(struct App *,uint32_t,uint32_t *,int32_t *,const char **);
void preparation_reply(struct App *,uint16_t,uint32_t);
bool preparation_pending(const struct App *);
bool preparation_has_room(const struct App *);
/* Coordinator-only atomic ownership transfer. Clears pointers it consumes. */
const char *app_preparation_admit(struct App *,struct AppOutput *,PreparedScene *);
int app_preparation_persist(struct App *,struct AppOutput *,PreparedScene *,StoreWrite *,unsigned *,uint8_t *,const char **);
int app_preparation_config_persist(struct App *,const PreparationRequest *,const void *,size_t,StoreWrite *,unsigned *,uint8_t *,const char **);
const char *app_preparation_config_commit(struct App *,const PreparationRequest *,HardwareConfig *,const void *,size_t);
const char *app_preparation_commit(struct App *,struct AppOutput *,PreparedScene *);
#endif
