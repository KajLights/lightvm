#include "vm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void finish(LbcInstance *i,unsigned quantum){unsigned calls=0;int result;do{result=lbc_resume(i,quantum);assert(++calls<100000);}while(result==0);assert(result==1);}
int main(int argc,char **argv){
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);rewind(f);uint8_t *raw=malloc((size_t)size);assert(raw);assert(fread(raw,1,(size_t)size,f)==(size_t)size);fclose(f);
    LbcProgram p;assert(lbc_open(&p,raw,(size_t)size));while(!lbc_validate(&p,1)){}
    uint8_t aa[600],bb[600];LbcBuffer a={aa,200,1,false},b={bb,200,2,false};LbcInstance x,y;
    assert(lbc_init(&x,&p,&a)&&lbc_init(&y,&p,&b));
    /* Changing presentation frequency must not change simulation. */
    for(unsigned k=1;k<=120;k++){
        assert(lbc_tick(&x,k/60.0f,1.0f/60.0f));finish(&x,1);
        assert(lbc_tick(&y,k/60.0f,1.0f/60.0f));finish(&y,256);
        assert(lbc_render(&x,k/60.0f));finish(&x,7);
        if(k%2==0){assert(lbc_render(&y,k/60.0f));finish(&y,256);assert(!memcmp(aa,bb,sizeof(aa)));}
        for(unsigned r=0;r<p.nregs;r++)if(p.registers[r*8+1]==R_STATE)assert(x.r[r].u==y.r[r].u);
    }
    /* A different slice size yields identical completed output and saved state. */
    assert(lbc_render(&x,2));assert(lbc_resume(&x,1)==0);unsigned saved_pc=x.pc;
    a.sealed=true;assert(lbc_resume(&x,256)==-1);assert(x.pc==saved_pc);assert(!lbc_begin(&x,3,1));
    a.sealed=false;assert(lbc_begin(&x,3,1.0f/60.0f));a.generation++;assert(lbc_resume(&x,256)==-1);
    /* Parameters cannot change mid-invocation or manufacture views. */
    assert(lbc_begin(&y,3,1.0f/60.0f));assert(!lbc_parameter(&y,5,0));finish(&y,256);assert(!lbc_parameter(&y,0,1));
    assert(x.program==y.program && x.buffer->data!=y.buffer->data);
    free(raw);puts("runtime ownership, continuation and cadence checks passed");return 0;
}
