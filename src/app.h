#ifndef DEMO_APP_H
#define DEMO_APP_H
#include "protocol.h"
#include "vm.h"
#include "store.h"
#include "network.h"
#include "composition.h"
#include "transition.h"
#include "command.h"
#if LIGHTVM_BOOT_SEQUENCE
#include "boot_sequence.h"
#endif

typedef enum { INDICATOR_STEADY, INDICATOR_SLOW_BLINK, INDICATOR_RAPID_BLINK, INDICATOR_010100 } IndicatorEffect;
typedef struct {
    uint8_t rgb[3],brightness;
    bool on;
    IndicatorEffect effect;
    uint64_t effect_started;
} IndicatorLight;
typedef struct AppOutput {
    HardwareOutput hardware;
    VmInstance vm;
    PixelBuffer pixels;
    Composition *composition;
    Transition *transition;
    uint32_t composition_revision;
    uint32_t light_revision;
    uint64_t light_save_due;
    char light_error[96];
    uint32_t layer_generation;
    char active_program[OBJECT_NAME_LIMIT + 1];
    uint32_t active_crc;
    /* Power is a latched output target, independent of VM clocks or pixel content.
       Recall separately owns destination semantics until a manual edit. */
    bool startup_pending;
    bool recall_owned, power_on;
    char selected_preset[OBJECT_NAME_LIMIT + 1];
    uint32_t selected_preset_crc;
    IndicatorLight *indicators;
    bool indicator_dirty,indicator_pending,indicator_fault;
    uint64_t indicator_retry;
} AppOutput;
typedef struct App {
    struct PreparationQueue *preparation;
#if LIGHTVM_BOOT_SEQUENCE
    BootSequence boot_sequence;
#endif
    void (*service_io)(void *); void *service_context;
    Network network;
    InputRegistry inputs;
    // Keep the active interface/settings intact until the commit reply has left.
    NetworkConfig pending_network;
    bool network_pending;
    uint64_t network_due, network_reply_source, source, upload_owner, connection_sequence;
    char controller_id[65], boot_id[33], network_error[160];
    char mqtt_error[160];
    bool mqtt_connected;
    AppOutput fallback;
    AppOutput **outputs;
    unsigned output_count;
    AppOutput *upload_output;
    Store store;
    HardwareConfig hardware;
    DigitalInputs digital_inputs;
    char config_json[CONFIG_LIMIT];
    uint32_t config_size, revision;
    uint8_t staging[CONFIG_LIMIT];
    StoreUpload upload;
    uint32_t expected, received, expected_crc;
    unsigned object_kind, schedule_cursor;
    uint32_t transition_sequence;
    uint32_t transition_program_generation;
    bool uploading, staged, booting;
    bool command_active, disconnect_pending, hardware_changing;
    uint64_t upload_deadline;
    char object_name[OBJECT_NAME_LIMIT + 1];
    uint32_t last_code;
    char boot_error[160];
    SendMessage send;
    void *transport;
} App;
AppOutput *app_output(App *, const char *id);
/* Saved identity of the visible, unmodified composition, or NULL. */
const char *app_output_preset(const App *, const AppOutput *);
const char *app_output_selected_preset(const App *, const AppOutput *);
bool app_output_power(const AppOutput *);
bool app_output_color_control(const AppOutput *);
size_t app_output_light_json(const App *,const AppOutput *,char *,size_t,bool);
size_t app_indicator_json(const AppOutput *,unsigned pixel,char *,size_t);
void app_init(App *app, SendMessage send, void *transport);
void app_destroy(App *app);
void app_tick(App *app);
// Detection only: latched for this boot, false when the feature is excluded.
bool app_boot_sequence_triggered(const App *app);
void app_command(void *context, uint8_t type, uint16_t id, const uint8_t *payload, size_t size);
void app_dispatch(App *, uint64_t source, SendMessage, void *, uint8_t, uint16_t, const uint8_t *, size_t);
void app_execute_command(App *,uint64_t source,SendMessage,void *,uint16_t,const Command *);
void app_disconnect(App *, uint64_t source);
void app_reply_delivered(App *, uint64_t source);
// Save an already validated network configuration on the engine owner.
enum Status app_save_network(App *, const NetworkConfig *, uint32_t revision,
                             uint64_t source, char *error, size_t capacity);
#endif
