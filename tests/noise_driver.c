#include "noise.h"
#include <inttypes.h>
#include <stdio.h>
int main(void){
    unsigned binding;uint32_t a[7];
    while(scanf("%u %" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx32,
                &binding,&a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&a[6])==8){
        uint32_t result=UINT32_C(0x5a5aa5a5);bool ok=lvm_noise_eval(binding,a,&result);
        printf("%u %08" PRIx32 "\n",ok,result);
    }
    return ferror(stdin)?1:0;
}
