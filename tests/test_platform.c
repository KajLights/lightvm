// Link the real engine against a test port, without host or Pico native code.
#include "session.h"
#include "pixels.h"
#include "vm_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
void platform_show_rgb(const uint8_t *rgb, size_t count) {
    CHECK(count == PIXEL_COUNT);
    memcpy(displayed, rgb, sizeof(displayed));
    ++shows;
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
    } else CHECK(false);
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
    static Session session;
    Capture reply = {.marker = 0x12345678u};
    session_init(&session, capture, &reply);
    CHECK(shows == 1 && displayed[0] == 0);
    command(&session, CMD_STATUS, NULL, 0);
    CHECK(reply.code == STATUS_OK && reply.id == 42);
    CHECK(strstr(reply.message, "api=1 protocol=1 platform=test board=virtual output=capture"));
    CHECK(strstr(reply.message, "pin=-1") && strstr(reply.message, "brightness=7"));
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
    return 0;
}
