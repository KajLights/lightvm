#include "release.h"
// Link the real engine against a test port, without host or Pico native code.
#include "session.h"
#include "pixels.h"
#include "vm_allocator.h"
#include "runtime.h"
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
static bool delayed_output, output_ready, output_failed;
static bool cooperative_show, secondary_output_allowed;
static const uint8_t *borrowed_output;
static void *borrowed_driver;
static size_t borrowed_count;
static uint32_t borrowed_crc;
static unsigned cancelled_outputs;
static uint8_t displayed[PIXEL_BYTES];
static const PlatformInfo info = {"test", "virtual", "capture"};
const PlatformInfo *platform_info(void) { return &info; }
uint64_t platform_time_us(void) {
    uint64_t now = clock_now;
    clock_now += clock_step;
    return now;
}
static const BoardPin pins[]={{"GP16",16},{"GP17",17}};
const BoardPin *platform_pins(size_t *count) { *count=2; return pins; }
bool platform_pixels_open(const PixelConfig *config,void **driver) {
    if (config->pin==17&&!secondary_output_allowed) return false;
    PixelConfig *p=calloc(1,sizeof(*p)); if (!p) return false;
    *p=*config; *driver=p; return true;
}
void platform_pixels_close(void *driver) { CHECK(driver!=borrowed_driver);free(driver); }
bool platform_pixels_show(void *driver,const uint8_t *rgb,size_t count) {
    CHECK(driver && count==((PixelConfig *)driver)->count);
    CHECK(count*3<=sizeof(displayed));
    if (deny_show) return false;
    if(cooperative_show)runtime_checkpoint();
    memcpy(displayed,rgb,count*3); ++shows; return true;
}
bool platform_pixels_submit(void *driver,const uint8_t *rgb,size_t count) {
    CHECK(!borrowed_output);
    if(!delayed_output)return platform_pixels_show(driver,rgb,count);
    borrowed_output=rgb;borrowed_driver=driver;borrowed_count=count;
    borrowed_crc=crc32_bytes(rgb,count*3);return true;
}
int platform_pixels_poll(void *driver) {
    if(!borrowed_output)return 1;
    CHECK(driver==borrowed_driver&&crc32_bytes(borrowed_output,borrowed_count*3)==borrowed_crc);
    if(!output_ready&&!output_failed)return 0;
    bool ok=!output_failed&&platform_pixels_show(driver,borrowed_output,borrowed_count);
    borrowed_output=NULL;borrowed_driver=NULL;return ok?1:-1;
}
void platform_pixels_cancel(void *driver) {
    if(!borrowed_output)return;
    CHECK(driver==borrowed_driver&&crc32_bytes(borrowed_output,borrowed_count*3)==borrowed_crc);
    borrowed_output=NULL;borrowed_driver=NULL;cancelled_outputs++;
}
static uint8_t flash[STORAGE_BYTES], baseline[STORAGE_BYTES];
static bool storage_available;
static int fail_after=-1;
static bool fail(void) { if (fail_after==0) return true; if (fail_after>0) --fail_after; return false; }
uint32_t platform_storage_capacity(void) { return STORAGE_BYTES; }
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
    CHECK(store_put(&s,STORE_ANIMATION,"effect","old data",8));
    CHECK(store_put(&s,STORE_CONFIGURATION,STORE_CONFIG,EMPTY_CONFIG,(uint32_t)strlen(EMPTY_CONFIG)));
    CHECK(store_put(&s,STORE_NETWORK_SETTINGS,STORE_NETWORK,"old network",11));
    memcpy(baseline,flash,sizeof(flash));store_destroy(&s);
    unsigned interrupted=0;
    for (int point=0;point<90;++point) {
        memcpy(flash,baseline,sizeof(flash)); fail_after=-1; CHECK(store_mount(&s));
        fail_after=point; bool ok=store_put(&s,STORE_ANIMATION,"effect","new data",8); fail_after=-1;
        Store recovered; CHECK(store_mount(&recovered));
        const StoreEntry *entry=store_find(&recovered,STORE_ANIMATION,"effect"); char bytes[8];
        CHECK(entry && store_read(&recovered,entry,0,bytes,8));
        CHECK(!memcmp(bytes,"old data",8) || !memcmp(bytes,"new data",8));
        CHECK(store_find(&recovered,STORE_CONFIGURATION,STORE_CONFIG) && recovered.config_revision==1);
        CHECK(store_find(&recovered,STORE_NETWORK_SETTINGS,STORE_NETWORK) && recovered.network_revision==1);
        if (!ok) ++interrupted;
        store_destroy(&s);store_destroy(&recovered);
    }
    CHECK(interrupted>8 && interrupted<40);
    // Configuration publication remains atomic without a separate boot selector.
    for(int point=0;point<30;point++){
        memcpy(flash,baseline,sizeof(flash));CHECK(store_mount(&s));
        fail_after=point;(void)store_put_config(&s,EMPTY_CONFIG,(uint32_t)strlen(EMPTY_CONFIG));fail_after=-1;
        Store recovered;CHECK(store_mount(&recovered));
        CHECK(recovered.config_revision==1||recovered.config_revision==2);
        CHECK(store_find(&recovered,STORE_ANIMATION,"effect")&&store_find(&recovered,STORE_NETWORK_SETTINGS,STORE_NETWORK));
        store_destroy(&recovered);store_destroy(&s);
    }
    // Abandoning the very first payload leaves a blank, usable catalogue.
    memset(flash,255,sizeof(flash));CHECK(store_mount(&s));StoreUpload upload={.pin=-1};
    CHECK(store_upload_begin(&s,&upload,STORE_ANIMATION,"incomplete",300,0));
    CHECK(store_upload_chunk(&upload,0,baseline,256));store_upload_cancel(&upload);
    store_destroy(&s);CHECK(store_mount(&s));uint32_t cursor=0;CHECK(!store_list(&s,STORE_ANY,&cursor));store_destroy(&s);
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
static void upload(Session *session, Capture *reply, const uint8_t *bytecode, size_t size) {
    uint8_t begin[8], chunk[FRAME_LIMIT];
    write_u32(begin, (uint32_t)size);
    write_u32(begin + 4, crc32_bytes(bytecode, size));
    command(session, CMD_BEGIN, begin, sizeof(begin));
    CHECK(reply->code == STATUS_OK);
    for(size_t at=0;at<size;) {
        size_t n=size-at;if(n>FRAME_LIMIT-4)n=FRAME_LIMIT-4;
        write_u32(chunk,(uint32_t)at);memcpy(chunk+4,bytecode+at,n);
        command(session,CMD_CHUNK,chunk,n+4);CHECK(reply->code==STATUS_OK);at+=n;
    }
    command(session, CMD_FINISH, NULL, 0);
}

#include "bytecode_fixtures.h"

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
    CHECK(store_put(&app.store,STORE_NETWORK_SETTINGS,STORE_NETWORK,"invalid",7));
    app_destroy(&app); app_init(&app,capture,&reply);
    CHECK(!app.network.saved_valid && app.network_error[0]);
    network_bootstrap(&app.network,true);
    CHECK(app.network.initial_setup && !app.network.config.ap_password[0]);
    app.network.driver.configure=radio_configure; app.network.driver.link=radio_link;
    NetworkConfig setup=app.network.config; network_apply(&app.network,&setup);
    network_tick(&app.network,0); network_tick(&app.network,1); CHECK(app.network.state==NET_AP_ACTIVE);
    session_init(&session,&app,1,capture,&reply);
    const char *candidate="{\"schema\":\"lightvm.network\",\"wifi\":{\"enabled\":false,\"mode\":\"ap\",\"ap\":{\"ssid\":\"saved\",\"password\":\"ap-secret\"}},\"http\":{\"enabled\":false,\"port\":8080}}";
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

#include "portable_contract.inc"
#include "composition_contract.inc"
#include "transition_contract.inc"

static unsigned service_calls;
static void advance_service(void *context) { (void)context;service_calls++;clock_now+=100; }
static void disconnect_service(void *context) { App *a=context;app_disconnect(a,a->upload_owner); }

int main(void) {
    storage_faults();storage_available=true;memset(flash,255,sizeof(flash));
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
    uint8_t partial[HEADER_SIZE+8]={'L','V','M','1',1,CMD_BEGIN,42,0};
    write_u32(partial+8,8); memcpy(partial+16,begin,8);
    uint32_t crc=crc32_update(0xffffffffu,partial+4,8); crc=crc32_update(crc,partial+16,8)^0xffffffffu; write_u32(partial+12,crc);
    session_feed(&session,partial,7); CHECK(!app.upload_owner);
    command(&other,CMD_STATUS,NULL,0); CHECK(other_reply.code==STATUS_OK && other_reply.id==42 && !app.upload_owner);
    session_feed(&session,partial+7,sizeof(partial)-7); CHECK(reply.code==STATUS_OK && app.upload_owner==1);
    command(&other,CMD_STATUS,NULL,0); CHECK(other_reply.code==STATUS_OK && other_reply.id==42);
    command(&other,CMD_OBJECT_CANCEL,NULL,0); CHECK(other_reply.code==STATUS_BUSY && app.upload_owner==1);
    command(&other,CMD_BEGIN,begin,sizeof(begin)); CHECK(other_reply.code==STATUS_BUSY);
    session_destroy(&session); CHECK(!app_output(&app,NULL)->vm.uploading && !app.upload_owner);
    session_init(&session,&app,1,capture,&reply);
    command(&other,CMD_BEGIN,begin,sizeof(begin)); CHECK(other_reply.code==STATUS_OK && app.upload_owner==2);
    clock_now=30000001u; app_tick(&app); CHECK(!app.upload_owner && !app_output(&app,NULL)->vm.uploading); clock_now=0;
    command(&session,CMD_RESET,NULL,0);

    char config[400], metadata[120];
    int config_size=snprintf(config,sizeof(config),"{\"schema\":\"lightvm.config\",\"outputs\":[{\"id\":\"lights\",\"type\":\"ws2812b\",\"config\":{\"pin\":\"GP16\",\"count\":%u,\"brightness_limit\":7}}]}",PIXEL_COUNT);
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
    CHECK(strstr(reply.message, "version=" LIGHTVM_VERSION " protocol=1 platform=test board=virtual output=capture"));
    CHECK(strstr(reply.message, "pin=16") && strstr(reply.message, "brightness=7"));
    // Decoder errors must use the same native transport context as app replies.
    uint8_t bad_header[HEADER_SIZE] = {'L','V','M','1',99,CMD_STATUS,7,0};
    session_feed(&session, bad_header, sizeof(bad_header));
    CHECK(reply.code == STATUS_PROTOCOL && reply.id == 7);

    command(&session, 10, NULL, 0);
    CHECK(reply.code == STATUS_PROTOCOL && !strcmp(reply.message, "unknown command"));
    CHECK(shows == 1);
    upload(&session, &reply, render_lbc, sizeof(render_lbc));
    CHECK(reply.code == STATUS_OK && vm_heap_used() > 0);
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

    command(&session,CMD_PLAY,NULL,0);CHECK(reply.code==STATUS_OK);
    unsigned live_frames=app_output(&app,NULL)->vm.frame_count;uint32_t generation=app_output(&app,NULL)->vm.generation;
    app.service_io=advance_service;upload(&session, &reply, fault_lbc, sizeof(fault_lbc));app.service_io=NULL;
    CHECK(reply.code == STATUS_LOAD && app_output(&app,NULL)->vm.ready); /* failed preparation retains old instance */
    CHECK(app_output(&app,NULL)->vm.generation==generation&&app_output(&app,NULL)->vm.playing&&app_output(&app,NULL)->vm.frame_count>live_frames&&service_calls>0);
    // A native link-loss callback may arrive at a flash checkpoint; cancellation
    // waits for the serialized command to release its upload workspace.
    write_u32(begin,sizeof(render_lbc));write_u32(begin+4,crc32_bytes(render_lbc,sizeof(render_lbc)));
    command(&session,CMD_BEGIN,begin,sizeof(begin));CHECK(reply.code==STATUS_OK);
    app.service_io=disconnect_service;app.service_context=&app;
    uint8_t interrupted[260]={0};memcpy(interrupted+4,render_lbc,100);
    command(&session,CMD_CHUNK,interrupted,104);app.service_io=NULL;app.service_context=NULL;
    CHECK(!app.upload_owner&&!app.upload.active&&app.upload.pin==-1&&!app_output(&app,NULL)->vm.uploading&&app_output(&app,NULL)->vm.ready);
    vm_destroy(&app_output(&app,NULL)->vm);vm_init(&app_output(&app,NULL)->vm,&app_output(&app,NULL)->pixels,capture,&reply);
    app_output(&app,NULL)->vm.memory=fault_lbc;app_output(&app,NULL)->vm.expected=sizeof(fault_lbc);CHECK(!vm_load(&app_output(&app,NULL)->vm));app_output(&app,NULL)->vm.ready=true;
    command(&session, CMD_FRAME, delta, sizeof(delta));
    CHECK(reply.code == STATUS_EXECUTION);
    command(&session, CMD_RESET, NULL, 0);
    CHECK(reply.code == STATUS_OK && vm_heap_used() == 0);
    deny_allocation = true;
    write_u32(begin,sizeof(render_lbc));write_u32(begin+4,crc32_bytes(render_lbc,sizeof(render_lbc)));
    command(&session,CMD_BEGIN,begin,sizeof(begin));
    CHECK(reply.code == STATUS_STORAGE && !app.upload_owner && vm_heap_used() == 0);
    deny_allocation = false;
    upload(&session, &reply, render_lbc, sizeof(render_lbc));
    CHECK(reply.code == STATUS_OK);
    // Control steps allocate only during preparation. The output buffer is
    // charged alongside interpreter/history/staging storage in the same budget.
    upload(&session,&reply,control_lbc,sizeof(control_lbc));
    CHECK(reply.code==STATUS_OK && app_output(&app,NULL)->vm.control.enabled);
    reply.snapshot_size=0; write_u32(delta,1);
    command(&session,CMD_FRAME,delta,sizeof(delta)); CHECK(reply.code==STATUS_OK);
    CHECK(app_output(&app,NULL)->vm.control.output_charge==PIXEL_BYTES);
    unsigned control_allocations=allocations;
    deny_allocation=true;
    for (unsigned i=0;i<100;++i) {
        reply.snapshot_size=0; command(&session,CMD_FRAME,delta,sizeof(delta));
        CHECK(reply.code==STATUS_OK);
    }
    CHECK(allocations==control_allocations);
    command(&session,CMD_PLAY,NULL,0); CHECK(reply.code==STATUS_OK);
    uint64_t before_time=app_output(&app,NULL)->vm.control.program_us;
    clock_now+=1500123u;
    app_tick(&app);
    CHECK(app_output(&app,NULL)->vm.control.program_us-before_time==1500123u);
    CHECK(displayed[0]==0 && displayed[1]==5 && displayed[2]==220);
    uint32_t before_frame=app_output(&app,NULL)->vm.control.output_frame;
    deny_show=true; reply.snapshot_size=0; write_u32(delta,7);
    command(&session,CMD_FRAME,delta,sizeof(delta));
    CHECK(reply.code==STATUS_EXECUTION && !app_output(&app,NULL)->vm.control.faulted && app_output(&app,NULL)->vm.playing);
    CHECK(app_output(&app,NULL)->vm.control.output_frame==before_frame);
    CHECK(displayed[2]==220 && app_output(&app,NULL)->pixels.rgb[2]==220);
    CHECK(app_output(&app,NULL)->vm.control.committed[2]==7); // successful batch survives driver failure
    deny_show=false; deny_allocation=false;
    command(&session,CMD_RESET,NULL,0); CHECK(vm_heap_used()==0);
    // Allocation failure during control preparation leaves management usable.
    vm_init(&app_output(&app,NULL)->vm,&app_output(&app,NULL)->pixels,capture,&reply);app_output(&app,NULL)->vm.memory=control_lbc;app_output(&app,NULL)->vm.expected=sizeof(control_lbc);
    CHECK(!vm_load(&app_output(&app,NULL)->vm));app_output(&app,NULL)->vm.ready=true;
    deny_allocation=true; reply.snapshot_size=0;
    command(&session,CMD_FRAME,delta,sizeof(delta)); CHECK(reply.code==STATUS_EXECUTION);
    CHECK(app_output(&app,NULL)->vm.control.output_charge==0);
    deny_allocation=false;
    // Admission also rejects an output reservation when the aggregate budget
    // is already occupied; it does not raise the limit or lose that accounting.
    size_t occupied=VM_HEAP_LIMIT-vm_heap_used()-PIXEL_BYTES+1;
    CHECK(vm_heap_charge(occupied));
    size_t admitted=vm_heap_used();
    reply.snapshot_size=0; command(&session,CMD_FRAME,delta,sizeof(delta));
    CHECK(reply.code==STATUS_EXECUTION && vm_heap_used()==admitted);
    CHECK(strstr(reply.message,"aggregate heap budget"));
    vm_heap_uncharge(occupied);
    session_destroy(&session); app_destroy(&app);
    CHECK(vm_heap_used() == 0);

    // Two independently owned buffers/instances exercise the future slot boundary.
    PixelBuffer first={0}, second={0};
    PixelConfig first_config={16,3,255,"GRB"}, second_config={16,7,255,"RGB"};
    CHECK(pixels_init(&first,&first_config,output_type("ws2812b")));
    CHECK(pixels_init(&second,&second_config,output_type("ws2812b")));
    CHECK(first.rgb!=second.rgb && first.driver!=second.driver);
    static VmInstance one,two;
    Capture first_reply={.marker=0x12345678u}, second_reply={.marker=0x12345678u};
    vm_init(&one,&first,capture,&first_reply); vm_init(&two,&second,capture,&second_reply);
    one.memory=render_lbc; one.expected=sizeof(render_lbc);
    two.memory=render_lbc; two.expected=sizeof(render_lbc);
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
    // physical driver. State, clock, settings and RGBA contributions are private.
    uint8_t canvas_one[9], canvas_two[21];
    memset(canvas_one,99,sizeof(canvas_one)); memset(canvas_two,99,sizeof(canvas_two));
    PixelBuffer offscreen_one={.rgb=canvas_one,.count=3}, offscreen_two={.rgb=canvas_two,.count=7};
    vm_init(&one,&offscreen_one,capture,&first_reply); vm_init(&two,&offscreen_two,capture,&second_reply);
    memcpy(one.slot,"A",2); memcpy(two.slot,"B",2);
    one.memory=slot_lbc; one.expected=sizeof(slot_lbc);
    two.memory=slot_lbc; two.expected=sizeof(slot_lbc);
    CHECK(!vm_load(&one) && !vm_load(&two)); one.ready=true; two.ready=true;
    unsigned offscreen_shows=shows;
    Json settings;
    const char *first_settings="{\"seed\":7}", *second_settings="{\"seed\":11}";
    CHECK(json_parse(&settings,(const uint8_t *)first_settings,strlen(first_settings)));
    CHECK(!control_settings(&one,&settings,0));
    CHECK(json_parse(&settings,(const uint8_t *)second_settings,strlen(second_settings)));
    CHECK(!control_settings(&two,&settings,0));
    CHECK(!control_prepare(&one) && !control_prepare(&two));
    CHECK(one.control.count==3 && two.control.count==7);
    CHECK(!one.output && !two.output && !one.control.output_charge && !two.control.output_charge);
    CHECK(one.control.allocation!=two.control.allocation);
    allocation_count=allocations; deny_allocation=true;
    CHECK(!control_step(&one,5000) && !control_step(&two,9000));
    CHECK(one.control.committed[10]==13 && two.control.committed[26]==21);
    CHECK(one.control.program_us==5000 && two.control.program_us==9000);
    CHECK(!control_step(&one,2000));
    CHECK(one.control.committed[10]==11 && two.control.committed[26]==21);
    CHECK(one.control.contribution_frame==3 && two.control.contribution_frame==2);
    CHECK(one.control.buffer_bytes==24 && two.control.buffer_bytes==56);
    CHECK(one.control.committed[0]==0 && two.control.committed[0]==0);
    CHECK(one.control.committed[11]==255 && two.control.committed[27]==255);
    CHECK(!one.control.output_valid && !two.control.output_valid && shows==offscreen_shows);
    for (size_t i=0;i<sizeof(canvas_one);++i) CHECK(canvas_one[i]==99);
    for (size_t i=0;i<sizeof(canvas_two);++i) CHECK(canvas_two[i]==99);
    CHECK(allocations==allocation_count); deny_allocation=false;
    vm_destroy(&one); CHECK(two.ready && vm_heap_used()>0);
    vm_destroy(&two); CHECK(vm_heap_used()==0 && shows==offscreen_shows);

    // Stored execution reads flash, not the resident upload array. The running
    // version remains pinned in flash through repeated catalogue replacements.
    storage_available=true;memset(flash,255,sizeof(flash));Store stored;CHECK(store_mount(&stored));
    CHECK(store_put(&stored,STORE_ANIMATION,"bytecode",slot_lbc,sizeof(slot_lbc)));
    const StoreEntry *entry=store_find(&stored,STORE_ANIMATION,"bytecode");CHECK(entry);
    vm_init(&one,&offscreen_one,capture,&first_reply);
    CHECK(vm_bind_storage(&one,&stored,entry));
    CHECK(!vm_load(&one));one.ready=true;CHECK(!control_prepare(&one));
    CHECK(!control_step(&one,5000));CHECK(one.control.committed[10]==10);
    uint32_t pinned=one.code.id;
    for(unsigned k=0;k<20;k++)CHECK(store_put(&stored,STORE_ANIMATION,"bytecode",render_lbc,sizeof(render_lbc)));
    CHECK(one.from_storage && one.code.id==pinned);
    CHECK(!control_step(&one,7000));
    CHECK(one.control.committed[10]==13);vm_destroy(&one);store_destroy(&stored);storage_available=false;

    // Initialization failure changes the active revision and leaves no driver.
    clock_now=0; app_init(&app,capture,&reply); session_init(&session,&app,1,capture,&reply);
    memcpy(app.staging,config,(size_t)config_size);
    char *pin=strstr((char *)app.staging,"GP16"); CHECK(pin); pin[3]='7';
    app.expected=(uint32_t)config_size; app.object_kind=1; app.staged=true; app.upload_owner=1;
    command(&session,CMD_MANAGE,(const uint8_t *)apply,strlen(apply));
    CHECK(reply.code==STATUS_CONFIG && app.revision==1 && !app_output(&app,NULL)->pixels.driver && !app_output(&app,NULL)->vm.target);
    CHECK(strstr(reply.message,"initialization failed"));
    session_destroy(&session); app_destroy(&app);
    network_transactions();
    portable_contract();
    composition_contract();
    transition_contract();
    return 0;
}
