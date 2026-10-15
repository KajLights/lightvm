/* Independent ownership, continuation, admission and flash read failure checks. */
#include "bytecode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode_fixtures.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static bool readable=true;
static unsigned reads;
static bool read_flash(void *context,uint32_t offset,void *bytes,size_t size) {
    (void)context;
    if(!readable || offset>sizeof(slot_lbc) || size>sizeof(slot_lbc)-offset)return false;
    ++reads;memcpy(bytes,slot_lbc+offset,size);return true;
}
static void finish(LbcInstance *i,unsigned quantum) {
    unsigned calls=0;int result;
    do { result=lbc_resume(i,quantum);CHECK(++calls<100000); } while(!result);
    CHECK(result==1);
}
int main(void) {
    LbcProgram p,q;CHECK(lbc_open_reader(&p,read_flash,NULL,sizeof(slot_lbc)));
    while(!lbc_validate(&p,1)){}
    CHECK(lbc_open(&q,slot_lbc,sizeof(slot_lbc)) && lbc_validate(&q,LBC_CODE)==1);
    uint8_t aa[21],bb[21];LbcBuffer a={aa,7,1,false},b={bb,7,2,false};LbcInstance x,y;
    CHECK(lbc_init(&x,&p,&a) && lbc_init(&y,&q,&b));
    for(unsigned k=1;k<=120;k++) {
        CHECK(lbc_tick(&x,k/60.0f,1.0f/60.0f));finish(&x,1);
        CHECK(lbc_tick(&y,k/60.0f,1.0f/60.0f));finish(&y,256);
        memset(aa,0,sizeof(aa));CHECK(lbc_render(&x,k/60.0f));finish(&x,7);
        if(k%2==0) { memset(bb,0,sizeof(bb));CHECK(lbc_render(&y,k/60.0f));finish(&y,256);CHECK(!memcmp(aa,bb,sizeof(aa))); }
        for(unsigned r=0;r<p.nregs;r++)if(p.registers[r*8+1]==R_STATE)CHECK(x.r[r].u==y.r[r].u);
    }
    CHECK(reads>1 && !p.raw && p.cache_size<=256);
    CHECK(lbc_render(&x,2));CHECK(!lbc_resume(&x,1));unsigned pc=x.pc;
    a.sealed=true;CHECK(lbc_resume(&x,256)==-1 && x.pc==pc);CHECK(!lbc_begin(&x,3,1));
    a.sealed=false;CHECK(lbc_begin(&x,3,1.0f/60));a.generation++;CHECK(lbc_resume(&x,256)==-1);
    CHECK(lbc_begin(&y,3,1.0f/60));CHECK(!lbc_parameter(&y,5,0));finish(&y,256);CHECK(!lbc_parameter(&y,0,1));
    a.generation--;CHECK(lbc_render(&x,4));readable=false;p.cache_size=0;
    CHECK(lbc_resume(&x,64)==-1);CHECK(!lbc_open_reader(&p,read_flash,NULL,sizeof(slot_lbc)));
    readable=true;
    CHECK(lbc_open(&p,loop_lbc,sizeof(loop_lbc)) && lbc_validate(&p,LBC_CODE)==1);
    CHECK(lbc_init(&x,&p,&a) && lbc_render(&x,0));int status;
    do { status=lbc_resume(&x,1024); } while(!status);
    CHECK(status==-1 && x.used==2000000);
    // Truncation, reserved bits, types and control-flow boundaries are rejected.
    _Alignas(4) uint8_t raw[sizeof(render_lbc)];memcpy(raw,render_lbc,sizeof(raw));
    CHECK(!lbc_open(&p,raw,sizeof(raw)-1));raw[40]=1;CHECK(!lbc_open(&p,raw,sizeof(raw)));
    memcpy(raw,render_lbc,sizeof(raw));raw[64]=T_COLOR;CHECK(!lbc_open(&p,raw,sizeof(raw)));
    memcpy(raw,render_lbc,sizeof(raw));unsigned code=64+lbc_u32(raw+8)*8;raw[code]=255;
    CHECK(lbc_open(&p,raw,sizeof(raw)) && lbc_validate(&p,LBC_CODE)==-1);
    CHECK(lbc_open(&p,control_lbc,sizeof(control_lbc)) && lbc_validate(&p,LBC_CODE)==1);
    puts("bytecode admission, continuation, ownership, flash reads and work limits passed");
    return 0;
}
