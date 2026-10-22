#include "native_math.h"
#include <stdio.h>
#include <errno.h>
#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#endif
int main(void){
#ifdef PICO_ON_DEVICE
    stdio_init_all();
    while(!stdio_usb_connected())tight_loop_contents();
#endif
    unsigned op,left,right;
    while(scanf("%u %x %x",&op,&left,&right)==3){
        uint32_t result=0x5a5aa5a5;
        errno=1729;
        bool ok=lvm_f32_eval(op,left,right,&result);
        if(errno!=1729)return 3;
        printf("%u %08x\n",ok,(unsigned)result);fflush(stdout);
    }
    return 0;
}
