#ifndef DEMO_APP_H
#define DEMO_APP_H
#include "protocol.h"
#include "vm.h"
#include "store.h"

typedef struct {
    VmInstance vm;
    PixelBuffer pixels;
    Store store;
    HardwareConfig hardware;
    char config_json[CONFIG_LIMIT];
    uint32_t config_size, revision;
    uint8_t staging[MODULE_LIMIT > CONFIG_LIMIT ? MODULE_LIMIT : CONFIG_LIMIT];
    uint32_t expected, received, expected_crc;
    unsigned object_kind;
    bool uploading, staged, booting;
    uint64_t upload_deadline;
    char object_name[OBJECT_NAME_LIMIT + 1], active_procedure[OBJECT_NAME_LIMIT + 1];
    uint32_t active_crc, last_code;
    char boot_error[160];
    SendMessage send;
    void *transport;
} App;
void app_init(App *app, SendMessage send, void *transport);
void app_destroy(App *app);
void app_tick(App *app);
void app_start_native_demo(App *app);
void app_command(void *context, uint8_t type, uint16_t id, const uint8_t *payload, size_t size);
#endif
