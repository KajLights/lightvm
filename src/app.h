#ifndef DEMO_APP_H
#define DEMO_APP_H
#include "protocol.h"
#include "vm.h"
#include "store.h"
#include "network.h"
#if LIGHTVM_BOOT_SEQUENCE
#include "boot_sequence.h"
#endif

typedef struct {
    HardwareDevice hardware;
    VmInstance vm;
    PixelBuffer pixels;
    char active_procedure[OBJECT_NAME_LIMIT + 1];
    uint32_t active_crc;
} AppDevice;
typedef struct {
#if LIGHTVM_BOOT_SEQUENCE
    BootSequence boot_sequence;
#endif
    void (*service_io)(void *); void *service_context;
    Network network;
    // Keep the active interface/settings intact until the commit reply has left.
    NetworkConfig pending_network;
    bool network_pending;
    uint64_t network_due, network_reply_source, source, upload_owner, connection_sequence;
    char device_id[65], boot_id[33], network_error[160];
    AppDevice fallback;
    AppDevice **devices;
    unsigned device_count;
    AppDevice *upload_device;
    Store store;
    HardwareConfig hardware;
    char config_json[CONFIG_LIMIT];
    uint32_t config_size, revision;
    uint8_t staging[CONFIG_LIMIT];
    StoreUpload upload;
    uint32_t expected, received, expected_crc;
    unsigned object_kind, schedule_cursor;
    bool uploading, staged, booting;
    bool command_active, disconnect_pending;
    uint64_t upload_deadline;
    char object_name[OBJECT_NAME_LIMIT + 1];
    uint32_t last_code;
    char boot_error[160];
    SendMessage send;
    void *transport;
} App;
AppDevice *app_device(App *, const char *id);
void app_init(App *app, SendMessage send, void *transport);
void app_destroy(App *app);
void app_tick(App *app);
// Detection only: latched for this boot, false when the feature is excluded.
bool app_boot_sequence_triggered(const App *app);
void app_command(void *context, uint8_t type, uint16_t id, const uint8_t *payload, size_t size);
void app_dispatch(App *, uint64_t source, SendMessage, void *, uint8_t, uint16_t, const uint8_t *, size_t);
void app_disconnect(App *, uint64_t source);
void app_reply_delivered(App *, uint64_t source);
#endif
