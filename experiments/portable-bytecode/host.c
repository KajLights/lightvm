#define _POSIX_C_SOURCE 200809L
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
static uint64_t ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
int main(int argc,char **argv){
    if(argc<2)return 2;
    int fd=open(argv[1],O_RDONLY);struct stat st;
    if(fd<0||fstat(fd,&st))return 2;
    if(st.st_size<=0)return 3;
    void *raw=mmap(NULL,(size_t)st.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    if(raw==MAP_FAILED)return 2;
    LbcProgram p;if(!lbc_open(&p,raw,(size_t)st.st_size))return 3;
    int valid;do{valid=lbc_validate(&p,16);}while(valid==0);if(valid<0)return 3;
    unsigned count=argc>2?(unsigned)atoi(argv[2]):200,frames=argc>3?(unsigned)atoi(argv[3]):1;
    if(!count||count>10000||!frames||frames>1000000)return 2;
    uint8_t *rgb=calloc(count,3),*native=calloc(count,3);LbcBuffer buffer={rgb,count,1,false};LbcInstance i;
    if(!rgb||!native||!lbc_init(&i,&p,&buffer))return 2;
    bool bench=argc>4&&!strcmp(argv[4],"bench");uint64_t vm_ns=0,native_ns=0;float phase=0,stride=1.0f/count;
    for(unsigned frame=0;frame<frames;frame++){
        uint64_t start=ns();if(!lbc_begin(&i,(frame+1)/60.0f,1.0f/60.0f))return 4;
        int result;do{result=lbc_resume(&i,256);}while(result==0);vm_ns+=ns()-start;if(result<0)return 4;
        if(bench){start=ns();phase=phase+1.0f/60.0f*0.1f;phase=phase-(float)(int)phase;
            for(unsigned k=0;k<count;k+=32)lbc_native_rainbow(native,k,k+32<count?k+32:count,phase,stride);
            native_ns+=ns()-start;if(memcmp(rgb,native,count*3)){fprintf(stderr,"mismatch frame %u\n",frame);return 5;}}
    }
    if(bench)printf("{\"frames\":%u,\"pixels\":%u,\"vm_ns_per_frame\":%.1f,\"native_ns_per_frame\":%.1f,\"ratio\":%.3f,\"instance_bytes\":%zu,\"program_metadata_bytes\":%zu,\"package_bytes\":%zu,\"identical\":true}\n",frames,count,(double)vm_ns/frames,(double)native_ns/frames,(double)vm_ns/native_ns,sizeof(i),sizeof(p),(size_t)st.st_size);
    else fwrite(rgb,3,count,stdout);
    free(rgb);free(native);munmap(raw,(size_t)st.st_size);close(fd);return 0;
}
