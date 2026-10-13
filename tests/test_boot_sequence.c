#include "boot_sequence.h"
#include "platform.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint8_t flash[BOOT_SEQUENCE_STORAGE_BYTES], baseline[BOOT_SEQUENCE_STORAGE_BYTES];
static uint64_t now;
static unsigned operations, writes, erases;
static bool available=true;
static int fail_after=-1;
static size_t torn_bytes=128;
static bool silent_failure;
uint64_t platform_time_us(void) { return now; }
static bool fail(void) {
    ++operations;
    if (fail_after==0) { fail_after=-1; return true; }
    if (fail_after>0) --fail_after;
    return false;
}
bool platform_boot_storage_available(void) { return available; }
bool platform_boot_storage_read(uint32_t offset,void *bytes,size_t size) {
    CHECK(offset<=sizeof(flash) && size<=sizeof(flash)-offset);
    if (fail()) return false;
    memcpy(bytes,flash+offset,size); return true;
}
bool platform_boot_storage_write(uint32_t offset,const void *bytes,size_t size) {
    CHECK(offset%256==0 && size==256 && offset+size<=sizeof(flash));
    ++writes;
    bool interrupted=fail();
    size_t length=interrupted ? torn_bytes : size;
    if (length>size) length=size;
    const uint8_t *p=bytes;
    for (size_t i=0;i<length;++i) { CHECK((flash[offset+i]&p[i])==p[i]); flash[offset+i]&=p[i]; }
    return !interrupted || silent_failure;
}
bool platform_boot_storage_erase(uint32_t offset,size_t size) {
    CHECK(offset%4096==0 && size==4096 && offset+size<=sizeof(flash));
    ++erases;
    bool interrupted=fail();
    memset(flash+offset,255,interrupted ? size/2 : size);
    return !interrupted || silent_failure;
}
bool platform_boot_storage_sync(void) { return !fail(); }
static void blank(void) {
    memset(flash,255,sizeof(flash)); now=123456789u;
    operations=writes=erases=0; fail_after=-1; torn_bytes=128; available=true; silent_failure=false;
}
static void sequence_and_timer(void) {
    blank(); BootSequence state;
    for (uint32_t count=1;count<=3;++count) {
        // Time spent powered off does not reset the previous persistent count.
        now+=86400000000ull;
        CHECK(boot_sequence_init(&state)); CHECK(state.count==count);
        CHECK(state.triggered==(count==3) && state.reset_pending && !state.error[0]);
    }
    unsigned before=operations;
    boot_sequence_tick(&state,state.started_us+BOOT_SEQUENCE_WINDOW_US-1);
    CHECK(operations==before && state.count==3 && state.triggered);
    boot_sequence_tick(&state,state.started_us+BOOT_SEQUENCE_WINDOW_US);
    CHECK(state.count==0 && state.triggered && !state.reset_pending);
    before=operations;
    boot_sequence_tick(&state,state.started_us+100000000u); CHECK(operations==before);
    CHECK(boot_sequence_init(&state) && state.count==1 && !state.triggered);
    // A normal first boot also clears its counter after five seconds.
    boot_sequence_tick(&state,state.started_us+BOOT_SEQUENCE_WINDOW_US);
    CHECK(boot_sequence_init(&state) && state.count==1 && !state.triggered);
    CHECK(erases==0);
}
static void rollover(void) {
    blank(); BootSequence state;
    for (unsigned i=0;i<100;++i) {
        CHECK(boot_sequence_init(&state)); CHECK(state.sequence==i+1);
        CHECK(state.count==(i<3 ? i+1 : 3));
    }
    CHECK(writes==100 && erases==6);
}
static void interrupted_boots(unsigned boots,unsigned points) {
    blank(); BootSequence state;
    for (unsigned i=0;i<boots;++i) CHECK(boot_sequence_init(&state));
    if (boots) boot_sequence_tick(&state,state.started_us+BOOT_SEQUENCE_WINDOW_US);
    memcpy(baseline,flash,sizeof(flash));
    // Read, program, erase, verification and sync failures all recover old/new.
    for (unsigned point=0;point<points;++point) {
        memcpy(flash,baseline,sizeof(flash)); fail_after=(int)point;
        bool ok=boot_sequence_init(&state);
        if (!ok) CHECK(!state.triggered && !state.reset_pending && state.error[0]);
        if (boots==31) CHECK(!memcmp(flash+4096,baseline+4096,4096));
        fail_after=-1;
        CHECK(boot_sequence_init(&state)); CHECK(state.count==1 || state.count==2);
        CHECK(!state.triggered && !state.error[0]);
    }
}
static void interrupted_reset(void) {
    blank(); BootSequence state;
    for (unsigned i=0;i<3;++i) CHECK(boot_sequence_init(&state));
    memcpy(baseline,flash,sizeof(flash));
    for (int point=0;point<4;++point) {
        memcpy(flash,baseline,sizeof(flash)); fail_after=-1;
        CHECK(boot_sequence_init(&state) && state.triggered);
        fail_after=point; boot_sequence_tick(&state,state.started_us+BOOT_SEQUENCE_WINDOW_US);
        CHECK(state.triggered && !state.reset_pending);
        unsigned before=operations;
        boot_sequence_tick(&state,state.started_us+10000000u); CHECK(before==operations);
        fail_after=-1; CHECK(boot_sequence_init(&state));
        CHECK(state.count==1 || state.count==3);
        CHECK(state.triggered==(state.count==3));
    }
}
static void corruption_and_errors(void) {
    blank(); BootSequence state;
    available=false; CHECK(!boot_sequence_init(&state)); CHECK(operations==0);
    CHECK(!state.triggered && !state.reset_pending && state.error[0]);
    available=true; CHECK(boot_sequence_init(&state)); CHECK(boot_sequence_init(&state));
    flash[256+252]^=1; // Ignore a corrupted latest record and consume its page.
    CHECK(boot_sequence_init(&state) && state.count==2 && state.next_page==3);
    blank(); memset(flash,0,sizeof(flash));
    CHECK(boot_sequence_init(&state) && state.count==1 && erases==1);
    // A failed first write is skipped on the next boot, at every byte boundary.
    for (size_t cut=0;cut<=256;++cut) {
        blank(); torn_bytes=cut; fail_after=32;
        CHECK(!boot_sequence_init(&state) && !state.triggered);
        fail_after=-1; CHECK(boot_sequence_init(&state));
        CHECK(state.count==(cut==256 ? 2u : 1u));
    }
    // Detect a native program/erase operation that incorrectly reports success.
    blank(); silent_failure=true; fail_after=32;
    CHECK(!boot_sequence_init(&state) && strstr(state.error,"verification"));
    blank();
    for (unsigned i=0;i<32;++i) CHECK(boot_sequence_init(&state));
    silent_failure=true; fail_after=32;
    CHECK(!boot_sequence_init(&state) && strstr(state.error,"erase verification"));
    blank(); CHECK(boot_sequence_init(&state));
    write_u32(flash+8,UINT32_MAX); write_u32(flash+12,UINT32_MAX);
    write_u32(flash+252,crc32_bytes(flash,252));
    CHECK(!boot_sequence_init(&state) && strstr(state.error,"exhausted") && !state.triggered);
}
int main(void) {
    sequence_and_timer(); rollover();
    interrupted_boots(0,38); interrupted_boots(1,38);
    interrupted_boots(31,58); interrupted_reset(); corruption_and_errors();
    return 0;
}
