// Link the real engine against a test port, without host or Pico native code.
#include "session.h"
#include "pixels.h"
#include "vm_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PIXEL_COUNT (PIXEL_CAPACITY == 175 ? 175u : 100u)
#define PIXEL_BYTES (PIXEL_COUNT * 3u)

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static uint64_t clock_now, clock_step;
static unsigned shows, allocations;
static bool deny_allocation;
static bool deny_show;
static uint8_t displayed[PIXEL_BYTES];
static const PlatformInfo info = {"test", "virtual", "capture", -1, 7};
const PlatformInfo *platform_info(void) { return &info; }
uint64_t platform_time_us(void) {
    uint64_t now = clock_now;
    clock_now += clock_step;
    return now;
}
static const BoardPin pins[]={{"GP16",16},{"GP17",17}};
const BoardPin *platform_pins(size_t *count) { *count=2; return pins; }
bool platform_pixels_open(const PixelConfig *config,void **driver) {
    if (config->pin==17) return false;
    PixelConfig *p=calloc(1,sizeof(*p)); if (!p) return false;
    *p=*config; *driver=p; return true;
}
void platform_pixels_close(void *driver) { free(driver); }
bool platform_pixels_show(void *driver,const uint8_t *rgb,size_t count) {
    CHECK(driver && count==((PixelConfig *)driver)->count);
    CHECK(count*3<=sizeof(displayed));
    if (deny_show) return false;
    memcpy(displayed,rgb,count*3); ++shows; return true;
}
static uint8_t flash[STORAGE_BYTES], baseline[STORAGE_BYTES];
static bool storage_available;
static int fail_after=-1;
static bool fail(void) { if (fail_after==0) return true; if (fail_after>0) --fail_after; return false; }
bool platform_storage_available(void) { return storage_available; }
bool platform_storage_read(uint32_t offset,void *bytes,size_t size) {
    CHECK(offset<=STORAGE_BYTES && size<=STORAGE_BYTES-offset);
    memcpy(bytes,flash+offset,size); return true;
}
bool platform_storage_write(uint32_t offset,const void *bytes,size_t size) {
    CHECK(offset%256==0 && size%256==0 && offset<=STORAGE_BYTES && size<=STORAGE_BYTES-offset);
    if (fail()) { memcpy(flash+offset,bytes,size/2); return false; }
    const uint8_t *p=bytes;
    for (size_t i=0;i<size;++i) { CHECK((flash[offset+i]&p[i])==p[i]); flash[offset+i]=p[i]; }
    return true;
}
bool platform_storage_erase(uint32_t offset,size_t size) {
    CHECK(offset%4096==0 && size%4096==0 && offset<=STORAGE_BYTES && size<=STORAGE_BYTES-offset);
    if (fail()) { memset(flash+offset,255,size/2); return false; }
    memset(flash+offset,255,size); return true;
}
bool platform_storage_sync(void) { return !fail(); }
#if LIGHTVM_BOOT_SEQUENCE
// This contract fixture has no boot metadata; the dedicated journal test owns
// fault injection for that independent region.
bool platform_boot_storage_available(void) { return false; }
bool platform_boot_storage_read(uint32_t o,void *b,size_t n) { (void)o; (void)b; (void)n; CHECK(false); return false; }
bool platform_boot_storage_write(uint32_t o,const void *b,size_t n) { (void)o; (void)b; (void)n; CHECK(false); return false; }
bool platform_boot_storage_erase(uint32_t o,size_t n) { (void)o; (void)n; CHECK(false); return false; }
bool platform_boot_storage_sync(void) { CHECK(false); return false; }
#endif
static void storage_faults(void) {
    storage_available=true; memset(flash,255,sizeof(flash));
    Store s; CHECK(store_mount(&s));
    CHECK(store_put(&s,"effect","old wasm",8));
    CHECK(store_put(&s,STORE_CONFIG,EMPTY_CONFIG,(uint32_t)strlen(EMPTY_CONFIG)));
    CHECK(store_put(&s,STORE_NETWORK,"old network",11));
    memcpy(baseline,flash,sizeof(flash));
    unsigned interrupted=0;
    for (int point=0;point<90;++point) {
        memcpy(flash,baseline,sizeof(flash)); fail_after=-1; CHECK(store_mount(&s));
        fail_after=point; bool ok=store_put(&s,"effect","new wasm",8); fail_after=-1;
        Store recovered; CHECK(store_mount(&recovered));
        const StoreEntry *entry=store_find(&recovered,"effect"); char bytes[8];
        CHECK(entry && store_read(&recovered,entry,0,bytes,8));
        CHECK(!memcmp(bytes,"old wasm",8) || !memcmp(bytes,"new wasm",8));
        CHECK(store_find(&recovered,STORE_CONFIG) && recovered.config_revision==1);
        CHECK(store_find(&recovered,STORE_NETWORK) && recovered.network_revision==1);
        if (!ok) ++interrupted;
    }
    CHECK(interrupted>60);
    storage_available=false;
}
void *platform_calloc(size_t count, size_t size) {
    ++allocations;
    return deny_allocation ? NULL : calloc(count, size);
}
void *platform_realloc(void *memory, size_t size) {
    ++allocations;
    return deny_allocation ? NULL : realloc(memory, size);
}
void platform_free(void *memory) { free(memory); }

typedef struct {
    uint32_t marker, code;
    int32_t value;
    uint16_t id;
    char message[FRAME_LIMIT];
    uint8_t snapshot[PIXEL_BYTES];
    size_t snapshot_size;
} Capture;
static void capture(void *context, uint8_t type, uint16_t id,
                     const uint8_t *payload, size_t size) {
    Capture *reply = context;
    CHECK(reply->marker == 0x12345678u);
    reply->id = id;
    if (type == RESP_STATUS) {
        CHECK(size >= 8 && size - 8 < sizeof(reply->message));
        reply->code = read_u32(payload);
        reply->value = (int32_t)read_u32(payload + 4);
        memcpy(reply->message, payload + 8, size - 8);
        reply->message[size - 8] = 0;
    } else if (type == RESP_PIXELS) {
        CHECK(size <= sizeof(reply->snapshot) - reply->snapshot_size);
        memcpy(reply->snapshot + reply->snapshot_size, payload, size);
        reply->snapshot_size += size;
    } else CHECK(type==RESP_DATA);
}
static void input(void *context, const uint8_t *bytes, size_t size) {
    session_feed(context, bytes, size);
}
static void command(Session *session, uint8_t type, const uint8_t *bytes, size_t size) {
    // write_frame delivers header and payload separately, exercising the session.
    write_frame(input, session, type, 42, bytes, size);
}
static void upload(Session *session, Capture *reply, const uint8_t *wasm, size_t size) {
    uint8_t begin[8], chunk[FRAME_LIMIT];
    CHECK(size + 4 <= sizeof(chunk));
    write_u32(begin, (uint32_t)size);
    write_u32(begin + 4, crc32_bytes(wasm, size));
    command(session, CMD_BEGIN, begin, sizeof(begin));
    if (reply->code != STATUS_OK) return;
    write_u32(chunk, 0);
    memcpy(chunk + 4, wasm, size);
    command(session, CMD_CHUNK, chunk, size + 4);
    CHECK(reply->code == STATUS_OK);
    command(session, CMD_FINISH, NULL, 0);
}

// (module (import "led" "rgb" (func $rgb (param i32 i32 i32)))
//   (func (export "render") (param i32)
//     i32.const 255 i32.const 128 i32.const 0 call $rgb))
static const uint8_t render_wasm[] = {
    0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00,0x01,0x0b,0x02,0x60,0x03,0x7f,
    0x7f,0x7f,0x00,0x60,0x01,0x7f,0x00,0x02,0x0b,0x01,0x03,0x6c,0x65,0x64,
    0x03,0x72,0x67,0x62,0x00,0x00,0x03,0x02,0x01,0x01,0x07,0x0a,0x01,0x06,
    0x72,0x65,0x6e,0x64,0x65,0x72,0x00,0x01,0x0a,0x0e,0x01,0x0c,0x00,0x41,
    0xff,0x01,0x41,0x80,0x01,0x41,0x00,0x10,0x00,0x0b
};
// (module (func (export "run") (loop $again br $again)))
static const uint8_t loop_wasm[] = {
    0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00,0x01,0x04,0x01,0x60,0x00,0x00,
    0x03,0x02,0x01,0x00,0x07,0x07,0x01,0x03,0x72,0x75,0x6e,0x00,0x00,0x0a,
    0x09,0x01,0x07,0x00,0x03,0x40,0x0c,0x00,0x0b,0x0b
};

// Original control-ABI-2 fixture. WAT and manifest are included for auditability.
// (module
//   (import "lightvm.control.v2" "resource" (func $resource (param i32) (result i32)))
//   (import "lightvm.control.v2" "write" (func $write (param i32 i64 i32)))
//   (func (export "control_step") (param $trigger i32) (param $time i64) (param $delta i64) (result i32)
//     (call $write (call $resource (i32.const 0)) (i64.const 0)
//       (i32.wrap_i64 (i64.div_s (local.get $delta) (i64.const 1000))))
//     (i32.const 0)))
// {"schema":"lightvm.program/3","abi":2,"pixels":{"format":"rgb8"},"parameters":[]}
static const uint8_t control_wasm[] = {
    0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00,0x01,0x13,0x03,0x60,0x01,0x7f,
    0x01,0x7f,0x60,0x03,0x7f,0x7e,0x7f,0x00,0x60,0x03,0x7f,0x7e,0x7e,0x01,
    0x7f,0x02,0x3a,0x02,0x12,0x6c,0x69,0x67,0x68,0x74,0x76,0x6d,0x2e,0x63,
    0x6f,0x6e,0x74,0x72,0x6f,0x6c,0x2e,0x76,0x32,0x08,0x72,0x65,0x73,0x6f,
    0x75,0x72,0x63,0x65,0x00,0x00,0x12,0x6c,0x69,0x67,0x68,0x74,0x76,0x6d,
    0x2e,0x63,0x6f,0x6e,0x74,0x72,0x6f,0x6c,0x2e,0x76,0x32,0x05,0x77,0x72,
    0x69,0x74,0x65,0x00,0x01,0x03,0x02,0x01,0x02,0x07,0x10,0x01,0x0c,0x63,
    0x6f,0x6e,0x74,0x72,0x6f,0x6c,0x5f,0x73,0x74,0x65,0x70,0x00,0x02,0x0a,
    0x15,0x01,0x13,0x00,0x41,0x00,0x10,0x00,0x42,0x00,0x20,0x02,0x42,0xe8,
    0x07,0x7f,0xa7,0x10,0x01,0x41,0x00,0x0b,0x00,0x34,0x04,0x6e,0x61,0x6d,
    0x65,0x01,0x12,0x02,0x00,0x08,0x72,0x65,0x73,0x6f,0x75,0x72,0x63,0x65,
    0x01,0x05,0x77,0x72,0x69,0x74,0x65,0x02,0x19,0x01,0x02,0x03,0x00,0x07,
    0x74,0x72,0x69,0x67,0x67,0x65,0x72,0x01,0x04,0x74,0x69,0x6d,0x65,0x02,
    0x05,0x64,0x65,0x6c,0x74,0x61,0x00,0x61,0x0f,0x6c,0x69,0x67,0x68,0x74,
    0x76,0x6d,0x2e,0x70,0x72,0x6f,0x67,0x72,0x61,0x6d,0x7b,0x22,0x73,0x63,
    0x68,0x65,0x6d,0x61,0x22,0x3a,0x22,0x6c,0x69,0x67,0x68,0x74,0x76,0x6d,
    0x2e,0x70,0x72,0x6f,0x67,0x72,0x61,0x6d,0x2f,0x33,0x22,0x2c,0x22,0x61,
    0x62,0x69,0x22,0x3a,0x32,0x2c,0x22,0x70,0x69,0x78,0x65,0x6c,0x73,0x22,
    0x3a,0x7b,0x22,0x66,0x6f,0x72,0x6d,0x61,0x74,0x22,0x3a,0x22,0x72,0x67,
    0x62,0x38,0x22,0x7d,0x2c,0x22,0x70,0x61,0x72,0x61,0x6d,0x65,0x74,0x65,
    0x72,0x73,0x22,0x3a,0x5b,0x5d,0x7d,
};

// Original control-ABI-2 fixture. WAT and manifest are included for auditability.
// (module
//   (import "lightvm.control.v2" "resource" (func $resource (param i32) (result i32)))
//   (import "lightvm.control.v2" "count" (func $count (param i32) (result i64)))
//   (import "lightvm.control.v2" "write" (func $write (param i32 i64 i32)))
//   (import "lightvm.control.v2" "param_int" (func $param (param i32) (result i64)))
//   (global $handle (mut i32) (i32.const 0))
//   (global $phase (mut i64) (i64.const 0))
//   (func (export "control_step") (param $trigger i32) (param $time i64) (param $delta i64) (result i32)
//     (if (i32.eqz (local.get $trigger))
//       (then (global.set $handle (call $resource (i32.const 0))))
//       (else
//         (global.set $phase (i64.add (global.get $phase) (i64.const 1)))
//         (call $write (global.get $handle)
//           (i64.sub (call $count (global.get $handle)) (i64.const 1))
//           (i32.wrap_i64 (i64.add (global.get $phase)
//             (i64.add (i64.div_s (local.get $delta) (i64.const 1000)) (call $param (i32.const 0))))))))
//     (i32.const 0)))
// {"schema":"lightvm.program/3","abi":2,"pixels":{"format":"rgb8"},"parameters":[{"name":"seed","type":"int","default":4}]}
static const uint8_t slot_wasm[] = {
    0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00,0x01,0x18,0x04,0x60,0x01,0x7f,
    0x01,0x7f,0x60,0x01,0x7f,0x01,0x7e,0x60,0x03,0x7f,0x7e,0x7f,0x00,0x60,
    0x03,0x7f,0x7e,0x7e,0x01,0x7f,0x02,0x74,0x04,0x12,0x6c,0x69,0x67,0x68,
    0x74,0x76,0x6d,0x2e,0x63,0x6f,0x6e,0x74,0x72,0x6f,0x6c,0x2e,0x76,0x32,
    0x08,0x72,0x65,0x73,0x6f,0x75,0x72,0x63,0x65,0x00,0x00,0x12,0x6c,0x69,
    0x67,0x68,0x74,0x76,0x6d,0x2e,0x63,0x6f,0x6e,0x74,0x72,0x6f,0x6c,0x2e,
    0x76,0x32,0x05,0x63,0x6f,0x75,0x6e,0x74,0x00,0x01,0x12,0x6c,0x69,0x67,
    0x68,0x74,0x76,0x6d,0x2e,0x63,0x6f,0x6e,0x74,0x72,0x6f,0x6c,0x2e,0x76,
    0x32,0x05,0x77,0x72,0x69,0x74,0x65,0x00,0x02,0x12,0x6c,0x69,0x67,0x68,
    0x74,0x76,0x6d,0x2e,0x63,0x6f,0x6e,0x74,0x72,0x6f,0x6c,0x2e,0x76,0x32,
    0x09,0x70,0x61,0x72,0x61,0x6d,0x5f,0x69,0x6e,0x74,0x00,0x01,0x03,0x02,
    0x01,0x03,0x06,0x0b,0x02,0x7f,0x01,0x41,0x00,0x0b,0x7e,0x01,0x42,0x00,
    0x0b,0x07,0x10,0x01,0x0c,0x63,0x6f,0x6e,0x74,0x72,0x6f,0x6c,0x5f,0x73,
    0x74,0x65,0x70,0x00,0x04,0x0a,0x34,0x01,0x32,0x00,0x20,0x00,0x45,0x04,
    0x40,0x41,0x00,0x10,0x00,0x24,0x00,0x05,0x23,0x01,0x42,0x01,0x7c,0x24,
    0x01,0x23,0x00,0x23,0x00,0x10,0x01,0x42,0x01,0x7d,0x23,0x01,0x20,0x02,
    0x42,0xe8,0x07,0x7f,0x41,0x00,0x10,0x03,0x7c,0x7c,0xa7,0x10,0x02,0x0b,
    0x41,0x00,0x0b,0x00,0x54,0x04,0x6e,0x61,0x6d,0x65,0x01,0x20,0x04,0x00,
    0x08,0x72,0x65,0x73,0x6f,0x75,0x72,0x63,0x65,0x01,0x05,0x63,0x6f,0x75,
    0x6e,0x74,0x02,0x05,0x77,0x72,0x69,0x74,0x65,0x03,0x05,0x70,0x61,0x72,
    0x61,0x6d,0x02,0x19,0x01,0x04,0x03,0x00,0x07,0x74,0x72,0x69,0x67,0x67,
    0x65,0x72,0x01,0x04,0x74,0x69,0x6d,0x65,0x02,0x05,0x64,0x65,0x6c,0x74,
    0x61,0x07,0x10,0x02,0x00,0x06,0x68,0x61,0x6e,0x64,0x6c,0x65,0x01,0x05,
    0x70,0x68,0x61,0x73,0x65,0x00,0x89,0x01,0x0f,0x6c,0x69,0x67,0x68,0x74,
    0x76,0x6d,0x2e,0x70,0x72,0x6f,0x67,0x72,0x61,0x6d,0x7b,0x22,0x73,0x63,
    0x68,0x65,0x6d,0x61,0x22,0x3a,0x22,0x6c,0x69,0x67,0x68,0x74,0x76,0x6d,
    0x2e,0x70,0x72,0x6f,0x67,0x72,0x61,0x6d,0x2f,0x33,0x22,0x2c,0x22,0x61,
    0x62,0x69,0x22,0x3a,0x32,0x2c,0x22,0x70,0x69,0x78,0x65,0x6c,0x73,0x22,
    0x3a,0x7b,0x22,0x66,0x6f,0x72,0x6d,0x61,0x74,0x22,0x3a,0x22,0x72,0x67,
    0x62,0x38,0x22,0x7d,0x2c,0x22,0x70,0x61,0x72,0x61,0x6d,0x65,0x74,0x65,
    0x72,0x73,0x22,0x3a,0x5b,0x7b,0x22,0x6e,0x61,0x6d,0x65,0x22,0x3a,0x22,
    0x73,0x65,0x65,0x64,0x22,0x2c,0x22,0x74,0x79,0x70,0x65,0x22,0x3a,0x22,
    0x69,0x6e,0x74,0x22,0x2c,0x22,0x64,0x65,0x66,0x61,0x75,0x6c,0x74,0x22,
    0x3a,0x34,0x7d,0x5d,0x7d,
};

void platform_identity(char *device,size_t capacity,char *boot,size_t boot_capacity) { snprintf(device,capacity,"test-device"); snprintf(boot,boot_capacity,"test-boot"); }

static unsigned radio_configurations;
static bool radio_configure(void *context,const NetworkConfig *config) { (void)context; (void)config; radio_configurations++; return true; }
static int radio_link(void *context,char *ip,size_t capacity,char *error,size_t error_capacity) {
    (void)context; (void)error; (void)error_capacity; snprintf(ip,capacity,"192.168.4.1"); return 1;
}
static void network_transactions(void) {
    static App app; Session session; Capture reply={.marker=0x12345678u};
    memset(flash,255,sizeof(flash)); storage_available=true; fail_after=-1; clock_now=0;
    app_init(&app,capture,&reply); CHECK(!app.network.saved_valid);
    CHECK(store_put(&app.store,STORE_NETWORK,"invalid",7));
    app_destroy(&app); app_init(&app,capture,&reply);
    CHECK(!app.network.saved_valid && app.network_error[0]);
    network_bootstrap(&app.network,true);
    CHECK(app.network.initial_setup && !app.network.config.ap_password[0]);
    app.network.driver.configure=radio_configure; app.network.driver.link=radio_link;
    NetworkConfig setup=app.network.config; network_apply(&app.network,&setup);
    network_tick(&app.network,0); network_tick(&app.network,1); CHECK(app.network.state==NET_AP_ACTIVE);
    session_init(&session,&app,1,capture,&reply);
    const char *candidate="{\"schema\":\"lightvm.network/1\",\"wifi\":{\"enabled\":false,\"mode\":\"ap\",\"ap\":{\"ssid\":\"saved\",\"password\":\"ap-secret\"}},\"http\":{\"enabled\":false,\"port\":8080}}";
    memcpy(app.staging,candidate,strlen(candidate)); app.expected=(uint32_t)strlen(candidate);
    app.staged=true; app.object_kind=3; app.upload_owner=1;
    const char *commit="{\"op\":\"network_commit\",\"expected_revision\":1}";
    fail_after=0; command(&session,CMD_MANAGE,(const uint8_t *)commit,strlen(commit)); fail_after=-1;
    CHECK(reply.code==STATUS_STORAGE && app.network.initial_setup && !app.network_pending && app.network.state==NET_AP_ACTIVE);
    command(&session,CMD_MANAGE,(const uint8_t *)commit,strlen(commit));
    CHECK(reply.code==STATUS_OK && app.network_pending && app.network.initial_setup && app.network.config.port==80 && app.network.config.wifi_enabled);
    unsigned applied=radio_configurations;
    app_reply_delivered(&app,1); uint64_t due=app.network_due;
    clock_now=due-1; app_tick(&app); CHECK(app.network_pending && radio_configurations==applied && app.network.state==NET_AP_ACTIVE);
    clock_now=due; app_tick(&app);
    CHECK(!app.network_pending && !app.network.initial_setup && app.network.saved_valid && app.network.state==NET_DISABLED && radio_configurations==applied+1);
    CHECK(app.network.config.port==8080 && !strcmp(app.network.config.ap_password,"ap-secret"));
    session_destroy(&session); app_destroy(&app); app_init(&app,capture,&reply);
    CHECK(app.network.saved_valid && !app.network.config.wifi_enabled && !strcmp(app.network.config.ap_password,"ap-secret"));
    network_bootstrap(&app.network,true); CHECK(!app.network.initial_setup && !app.network.config.wifi_enabled);
    // Recovery saves update durable settings without reconfiguring the live AP.
    app.network.recovery=true; app.network.driver.configure=radio_configure; app.network.driver.link=radio_link;
    setup=app.network.config; network_apply(&app.network,&setup);
    network_tick(&app.network,0); network_tick(&app.network,1); CHECK(app.network.state==NET_AP_ACTIVE);
    session_init(&session,&app,1,capture,&reply);
    memcpy(app.staging,candidate,strlen(candidate)); app.expected=(uint32_t)strlen(candidate);
    app.staged=true; app.object_kind=3; app.upload_owner=1;
    commit="{\"op\":\"network_commit\",\"expected_revision\":2}";
    applied=radio_configurations;
    fail_after=0; command(&session,CMD_MANAGE,(const uint8_t *)commit,strlen(commit)); fail_after=-1;
    CHECK(reply.code==STATUS_STORAGE && app.network.recovery && app.network.state==NET_AP_ACTIVE && !app.network_pending);
    command(&session,CMD_MANAGE,(const uint8_t *)commit,strlen(commit));
    CHECK(reply.code==STATUS_OK && strstr(reply.message,"next boot") && !app.network_pending && app.network.recovery);
    clock_now+=1000000; app_tick(&app);
    CHECK(app.network.state==NET_AP_ACTIVE && network_http_port(&app.network)==80 && radio_configurations==applied);
    CHECK(app.network.config.port==8080 && !strcmp(app.network.config.ap_password,"ap-secret"));
    session_destroy(&session); app_destroy(&app);
}

int main(void) {
    storage_faults();
    static App app;
    static Session session;
    Capture reply = {.marker = 0x12345678u};
    app_init(&app,capture,&reply);
    CHECK(!app_boot_sequence_triggered(&app));
    session_init(&session,&app,1,capture,&reply);
    CHECK(shows == 0);
    // Equal request IDs on independent, fragmented decoders retain reply ownership.
    Session other; Capture other_reply={.marker=0x12345678u}; session_init(&other,&app,2,capture,&other_reply);
    uint8_t begin[8]; write_u32(begin,8); write_u32(begin+4,0);
    uint8_t partial[HEADER_SIZE+8]={'W','A','S','M',1,CMD_BEGIN,42,0};
    write_u32(partial+8,8); memcpy(partial+16,begin,8);
    uint32_t crc=crc32_update(0xffffffffu,partial+4,8); crc=crc32_update(crc,partial+16,8)^0xffffffffu; write_u32(partial+12,crc);
    session_feed(&session,partial,7); CHECK(!app.upload_owner);
    command(&other,CMD_STATUS,NULL,0); CHECK(other_reply.code==STATUS_OK && other_reply.id==42 && !app.upload_owner);
    session_feed(&session,partial+7,sizeof(partial)-7); CHECK(reply.code==STATUS_OK && app.upload_owner==1);
    command(&other,CMD_STATUS,NULL,0); CHECK(other_reply.code==STATUS_OK && other_reply.id==42);
    command(&other,CMD_OBJECT_CANCEL,NULL,0); CHECK(other_reply.code==STATUS_BUSY && app.upload_owner==1);
    command(&other,CMD_BEGIN,begin,sizeof(begin)); CHECK(other_reply.code==STATUS_BUSY);
    session_destroy(&session); CHECK(!app.vm.uploading && !app.upload_owner);
    session_init(&session,&app,1,capture,&reply);
    command(&other,CMD_BEGIN,begin,sizeof(begin)); CHECK(other_reply.code==STATUS_OK && app.upload_owner==2);
    clock_now=30000001u; app_tick(&app); CHECK(!app.upload_owner && !app.vm.uploading); clock_now=0;
    command(&session,CMD_RESET,NULL,0);

    char config[400], metadata[120];
    int config_size=snprintf(config,sizeof(config),"{\"schema\":\"lightvm.config/1\",\"devices\":[{\"id\":\"lights\",\"type\":\"ws2812b\",\"config\":{\"pin\":\"GP16\",\"count\":%u,\"brightness_limit\":7}}],\"targets\":{\"A\":{\"device\":\"lights\"}}}",PIXEL_COUNT);
    int metadata_size=snprintf(metadata,sizeof(metadata),"{\"kind\":\"config\",\"size\":%d,\"crc32\":%lu}",config_size,(unsigned long)crc32_bytes((const uint8_t *)config,(size_t)config_size));
    command(&session,CMD_OBJECT_BEGIN,(const uint8_t *)metadata,(size_t)metadata_size);
    CHECK(reply.code==STATUS_OK);
    uint8_t config_chunk[FRAME_LIMIT]; write_u32(config_chunk,0); memcpy(config_chunk+4,config,(size_t)config_size);
    command(&session,CMD_OBJECT_CHUNK,config_chunk,(size_t)config_size+4); CHECK(reply.code==STATUS_OK);
    command(&session,CMD_OBJECT_FINISH,NULL,0); CHECK(reply.code==STATUS_OK);
    const char *apply="{\"op\":\"config_apply\",\"expected_revision\":0}";
    command(&session,CMD_MANAGE,(const uint8_t *)apply,strlen(apply)); CHECK(reply.code==STATUS_OK);
    CHECK(shows==1 && displayed[0]==0);
    command(&session, CMD_STATUS, NULL, 0);
    CHECK(reply.code == STATUS_OK && reply.id == 42);
    CHECK(strstr(reply.message, "api=1 protocol=1 platform=test board=virtual output=capture"));
    CHECK(strstr(reply.message, "pin=16") && strstr(reply.message, "brightness=7"));
    // Decoder errors must use the same native transport context as app replies.
    uint8_t bad_header[HEADER_SIZE] = {'W','A','S','M',99,CMD_STATUS,7,0};
    session_feed(&session, bad_header, sizeof(bad_header));
    CHECK(reply.code == STATUS_PROTOCOL && reply.id == 7);

    command(&session, 10, NULL, 0);
    CHECK(reply.code == STATUS_PROTOCOL && !strcmp(reply.message, "unknown command"));
    CHECK(shows == 1);
    upload(&session, &reply, render_wasm, sizeof(render_wasm));
    CHECK(reply.code == STATUS_OK && allocations > 0 && vm_heap_used() > 0);
    command(&session, CMD_PLAY, NULL, 0);
    CHECK(reply.code == STATUS_OK);
    unsigned playback_shows = shows;
    app_tick(&app);
    CHECK(shows == playback_shows + 1);
    clock_now = FRAME_INTERVAL_US - 1;
    app_tick(&app);
    CHECK(shows == playback_shows + 1);
    clock_now = FRAME_INTERVAL_US;
    app_tick(&app);
    CHECK(shows == playback_shows + 2);
    command(&session, CMD_STOP, NULL, 0);
    CHECK(displayed[0] == 0);

    uint8_t delta[4] = {0};
    command(&session, CMD_FRAME, delta, sizeof(delta));
    CHECK(reply.code == STATUS_OK && reply.value == PIXEL_COUNT);
    CHECK(reply.snapshot_size == PIXEL_BYTES);
    for (size_t i = 0; i < PIXEL_BYTES; i += 3) {
        CHECK(displayed[i] == 255 && displayed[i + 1] == 128 && displayed[i + 2] == 0);
    }
    CHECK(!memcmp(displayed, reply.snapshot, PIXEL_BYTES));

    upload(&session, &reply, loop_wasm, sizeof(loop_wasm));
    CHECK(reply.code == STATUS_OK);
    clock_step = RUN_TIMEOUT_US / 4 + 1;
    command(&session, CMD_RUN, NULL, 0);
    CHECK(reply.code == STATUS_LIMIT && strstr(reply.message, "deadline"));
    clock_step = 0;
    command(&session, CMD_RESET, NULL, 0);
    CHECK(reply.code == STATUS_OK && vm_heap_used() == 0);
    deny_allocation = true;
    upload(&session, &reply, render_wasm, sizeof(render_wasm));
    CHECK((reply.code == STATUS_LOAD || reply.code == STATUS_UPLOAD) && vm_heap_used() == 0);
    deny_allocation = false;
    upload(&session, &reply, render_wasm, sizeof(render_wasm));
    CHECK(reply.code == STATUS_OK);
    // Control steps allocate only during preparation. The output buffer is
    // charged alongside interpreter/history/staging storage in the same budget.
    upload(&session,&reply,control_wasm,sizeof(control_wasm));
    CHECK(reply.code==STATUS_OK && app.vm.control.enabled);
    reply.snapshot_size=0; write_u32(delta,1);
    command(&session,CMD_FRAME,delta,sizeof(delta)); CHECK(reply.code==STATUS_OK);
    CHECK(app.vm.control.output_charge==PIXEL_BYTES);
    unsigned control_allocations=allocations;
    deny_allocation=true;
    for (unsigned i=0;i<100;++i) {
        reply.snapshot_size=0; command(&session,CMD_FRAME,delta,sizeof(delta));
        CHECK(reply.code==STATUS_OK);
    }
    CHECK(allocations==control_allocations);
    command(&session,CMD_PLAY,NULL,0); CHECK(reply.code==STATUS_OK);
    uint64_t before_time=app.vm.control.program_us;
    clock_now+=1500123u;
    app_tick(&app);
    CHECK(app.vm.control.program_us-before_time==1500123u);
    CHECK(displayed[0]==0 && displayed[1]==5 && displayed[2]==220);
    uint32_t before_frame=app.vm.control.output_frame;
    deny_show=true; reply.snapshot_size=0; write_u32(delta,7);
    command(&session,CMD_FRAME,delta,sizeof(delta));
    CHECK(reply.code==STATUS_RUN && app.vm.control.faulted && !app.vm.playing);
    CHECK(app.vm.control.output_frame==before_frame);
    CHECK(displayed[2]==220 && app.pixels.rgb[2]==220);
    CHECK(app.vm.control.committed[2]==7); // successful batch survives driver failure
    deny_show=false; deny_allocation=false;
    command(&session,CMD_RESET,NULL,0); CHECK(vm_heap_used()==0);
    // Allocation failure during control preparation leaves management usable.
    upload(&session,&reply,control_wasm,sizeof(control_wasm)); CHECK(reply.code==STATUS_OK);
    deny_allocation=true; reply.snapshot_size=0;
    command(&session,CMD_FRAME,delta,sizeof(delta)); CHECK(reply.code==STATUS_RUN);
    CHECK(app.vm.control.output_charge==0);
    deny_allocation=false;
    // Admission also rejects an output reservation when the aggregate budget
    // is already occupied; it does not raise the limit or lose that accounting.
    size_t occupied=VM_HEAP_LIMIT-vm_heap_used()-PIXEL_BYTES+1;
    CHECK(vm_heap_charge(occupied));
    size_t admitted=vm_heap_used();
    reply.snapshot_size=0; command(&session,CMD_FRAME,delta,sizeof(delta));
    CHECK(reply.code==STATUS_RUN && vm_heap_used()==admitted);
    CHECK(strstr(reply.message,"aggregate heap budget"));
    vm_heap_uncharge(occupied);
    session_destroy(&session); app_destroy(&app);
    CHECK(vm_heap_used() == 0);

    // Two independently owned buffers/instances exercise the future slot boundary.
    PixelBuffer first={0}, second={0};
    PixelConfig first_config={16,3,255,"GRB"}, second_config={16,7,255,"RGB"};
    CHECK(pixels_init(&first,&first_config,device_type("ws2812b")));
    CHECK(pixels_init(&second,&second_config,device_type("ws2812b")));
    CHECK(first.rgb!=second.rgb && first.driver!=second.driver);
    static VmInstance one,two;
    Capture first_reply={.marker=0x12345678u}, second_reply={.marker=0x12345678u};
    vm_init(&one,&first,capture,&first_reply); vm_init(&two,&second,capture,&second_reply);
    CHECK(vm_reserve_upload(&one,sizeof(render_wasm)) && vm_reserve_upload(&two,sizeof(render_wasm)));
    memcpy(one.bytes,render_wasm,sizeof(render_wasm)); one.expected=sizeof(render_wasm);
    memcpy(two.bytes,render_wasm,sizeof(render_wasm)); two.expected=sizeof(render_wasm);
    CHECK(!vm_load(&one) && !vm_load(&two)); one.ready=true; two.ready=true;
    vm_command(&one,CMD_FRAME,42,delta,sizeof(delta));
    CHECK(first_reply.code==STATUS_OK && first_reply.value==3 && second.rgb[0]==0);
    vm_command(&two,CMD_FRAME,42,delta,sizeof(delta));
    CHECK(second_reply.code==STATUS_OK && second_reply.value==7 && first.rgb[0]==255);
    unsigned allocation_count=allocations;
    for (unsigned i=0;i<4;++i) {
        first_reply.snapshot_size=0; vm_command(&one,CMD_FRAME,42,delta,sizeof(delta));
        second_reply.snapshot_size=0; vm_command(&two,CMD_FRAME,42,delta,sizeof(delta));
    }
    CHECK(allocations==allocation_count);
    vm_destroy(&one); CHECK(two.ready && vm_heap_used()>0);
    vm_destroy(&two); pixels_destroy(&first); pixels_destroy(&second);
    CHECK(vm_heap_used()==0);

    // One identical artifact renders into arbitrary assigned canvases, with no
    // physical driver. State, clock, parameters and RGB frames are private.
    uint8_t canvas_one[9], canvas_two[21];
    memset(canvas_one,99,sizeof(canvas_one)); memset(canvas_two,99,sizeof(canvas_two));
    PixelBuffer offscreen_one={.rgb=canvas_one,.count=3}, offscreen_two={.rgb=canvas_two,.count=7};
    vm_init(&one,&offscreen_one,capture,&first_reply); vm_init(&two,&offscreen_two,capture,&second_reply);
    memcpy(one.slot,"A",2); memcpy(two.slot,"B",2);
    CHECK(vm_reserve_upload(&one,sizeof(slot_wasm)) && vm_reserve_upload(&two,sizeof(slot_wasm)));
    memcpy(one.bytes,slot_wasm,sizeof(slot_wasm)); one.expected=sizeof(slot_wasm);
    memcpy(two.bytes,slot_wasm,sizeof(slot_wasm)); two.expected=sizeof(slot_wasm);
    CHECK(!vm_load(&one) && !vm_load(&two)); one.ready=true; two.ready=true;
    unsigned offscreen_shows=shows;
    CHECK(!control_prepare(&one) && !control_prepare(&two));
    CHECK(one.control.count==3 && two.control.count==7);
    CHECK(!one.output && !two.output && !one.control.output_charge && !two.control.output_charge);
    CHECK(one.control.allocation!=two.control.allocation);
    one.control.parameters[0].value.integer=7; two.control.parameters[0].value.integer=11;
    allocation_count=allocations; deny_allocation=true;
    CHECK(!control_step(&one,1,5000) && !control_step(&two,1,9000));
    CHECK(one.control.committed[8]==13 && two.control.committed[20]==21);
    CHECK(one.control.program_us==5000 && two.control.program_us==9000);
    CHECK(!control_step(&one,1,2000));
    CHECK(one.control.committed[8]==11 && two.control.committed[20]==21);
    CHECK(one.control.contribution_frame==3 && two.control.contribution_frame==2);
    CHECK(one.control.buffer_bytes==18 && two.control.buffer_bytes==42);
    CHECK(one.control.committed[0]==0 && two.control.committed[0]==0);
    CHECK(!one.control.output_valid && !two.control.output_valid && shows==offscreen_shows);
    for (size_t i=0;i<sizeof(canvas_one);++i) CHECK(canvas_one[i]==99);
    for (size_t i=0;i<sizeof(canvas_two);++i) CHECK(canvas_two[i]==99);
    CHECK(allocations==allocation_count); deny_allocation=false;
    vm_destroy(&one); CHECK(two.ready && vm_heap_used()>0);
    vm_destroy(&two); CHECK(vm_heap_used()==0 && shows==offscreen_shows);

    // Initialization failure changes the active revision and leaves no driver.
    clock_now=0; app_init(&app,capture,&reply); session_init(&session,&app,1,capture,&reply);
    memcpy(app.staging,config,(size_t)config_size);
    char *pin=strstr((char *)app.staging,"GP16"); CHECK(pin); pin[3]='7';
    app.expected=(uint32_t)config_size; app.object_kind=1; app.staged=true; app.upload_owner=1;
    command(&session,CMD_MANAGE,(const uint8_t *)apply,strlen(apply));
    CHECK(reply.code==STATUS_CONFIG && app.revision==1 && !app.pixels.driver && !app.vm.target);
    CHECK(strstr(reply.message,"initialization failed"));
    session_destroy(&session); app_destroy(&app);
    network_transactions();
    return 0;
}
