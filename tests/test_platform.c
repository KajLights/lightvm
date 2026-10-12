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
static void storage_faults(void) {
    storage_available=true; memset(flash,255,sizeof(flash));
    Store s; CHECK(store_mount(&s));
    CHECK(store_put(&s,"effect","old wasm",8));
    CHECK(store_put(&s,STORE_CONFIG,EMPTY_CONFIG,(uint32_t)strlen(EMPTY_CONFIG)));
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
    CHECK(reply->code == STATUS_OK);
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

int main(void) {
    storage_faults();
    static Session session;
    Capture reply = {.marker = 0x12345678u};
    session_init(&session, capture, &reply);
    CHECK(shows == 0);
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

    command(&session, CMD_NATIVE_DEMO, NULL, 0);
    session_tick(&session);
    CHECK(shows == 2);
    clock_now = FRAME_INTERVAL_US - 1;
    session_tick(&session);
    CHECK(shows == 2);
    clock_now = FRAME_INTERVAL_US;
    session_tick(&session);
    CHECK(shows == 3);
    command(&session, CMD_STOP, NULL, 0);
    CHECK(displayed[0] == 0);

    upload(&session, &reply, render_wasm, sizeof(render_wasm));
    CHECK(reply.code == STATUS_OK && allocations > 0 && vm_heap_used() > 0);
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
    CHECK(reply.code == STATUS_LOAD && vm_heap_used() == 0);
    deny_allocation = false;
    upload(&session, &reply, render_wasm, sizeof(render_wasm));
    CHECK(reply.code == STATUS_OK);
    session_destroy(&session);
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

    // Initialization failure changes the active revision and leaves no driver.
    clock_now=0; session_init(&session,capture,&reply);
    memcpy(session.app.staging,config,(size_t)config_size);
    char *pin=strstr((char *)session.app.staging,"GP16"); CHECK(pin); pin[3]='7';
    session.app.expected=(uint32_t)config_size; session.app.object_kind=1; session.app.staged=true;
    command(&session,CMD_MANAGE,(const uint8_t *)apply,strlen(apply));
    CHECK(reply.code==STATUS_CONFIG && session.app.revision==1 && !session.app.pixels.driver && !session.app.vm.target);
    CHECK(strstr(reply.message,"initialization failed"));
    session_destroy(&session);
    return 0;
}
