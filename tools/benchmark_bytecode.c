/* Matched f32 rainbow comparison using the production interpreter and native C.
 * Host timing only; no timing claim is made for a microcontroller. */
#include "bytecode.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static uint8_t package[LBC_MAX_BYTES];
static size_t package_size;
static bool read_package(void *ctx,uint32_t offset,void *out,size_t size) {
    (void)ctx;
    if(offset>package_size || size>package_size-offset)return false;
    memcpy(out,package+offset,size);return true;
}
static double clock_ns(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec*1e9+t.tv_nsec;
}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"usage: benchmark_bytecode rainbow.lbc\n");return 2;}
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    package_size=fread(package,1,sizeof(package),f);bool too_large=fgetc(f)!=EOF;fclose(f);
    LbcProgram program;
    if(too_large || !lbc_open_reader(&program,read_package,NULL,package_size) || lbc_validate(&program,LBC_CODE)!=1)return 2;
    enum { COUNT=200, FRAMES=3000 };
    uint8_t pixels[COUNT*3],reference[COUNT*3];
    LbcBuffer view={.data=pixels,.count=COUNT,.generation=1};LbcInstance instance;
    if(!lbc_init(&instance,&program,&view))return 2;
    double interpreted=0,native=0;float phase=0;
    for(unsigned frame=1;frame<=FRAMES;frame++) {
        double start=clock_ns();
        if(!lbc_begin(&instance,(float)frame/60.0f,1.0f/60.0f))return 2;
        int result;while(!(result=lbc_resume(&instance,64))){}
        interpreted+=clock_ns()-start;if(result<0)return 2;
        start=clock_ns();phase+=1.0f/60.0f*0.1f;phase-=floorf(phase);
        lbc_native_rainbow(reference,0,COUNT,phase,1.0f/COUNT);
        native+=clock_ns()-start;
        if(memcmp(pixels,reference,sizeof(pixels))){fprintf(stderr,"mismatch at frame %u\n",frame);return 1;}
    }
    printf("{\"scope\":\"host only\",\"pixels\":%u,\"frames\":%u,\"mismatches\":0,\"bytecode_us_mean\":%.3f,\"native_us_mean\":%.3f,\"ratio\":%.3f,\"program_state_bytes\":%zu,\"instance_bytes\":%zu}\n",COUNT,FRAMES,interpreted/FRAMES/1000,native/FRAMES/1000,interpreted/native,sizeof(program),sizeof(instance));
    return 0;
}
