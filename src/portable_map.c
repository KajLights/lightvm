/* Generic execution of compiler-recognized pixel maps. No effect policies. */
#include "portable_map.h"
#include "draw.h"
#include "render_executor.h"
#include <string.h>
#include <limits.h>
#include <math.h>

enum { MAP_STORE=34, MAP_LENGTH=35, MAP_NATIVE=38, MAP_SIN=100, MAP_RED, MAP_GREEN, MAP_BLUE, MAP_WHEEL, MAP_FEQ, MAP_EQ, MAP_LITERAL };
typedef union { uint32_t u; int32_t i; float f; } Value;
typedef struct { uint8_t op,a,b,c; uint32_t d; } Instruction;

static bool decode(Portable *p,unsigned pc,Instruction *ins) {
    uint8_t q[8];if(!pbc_read_instruction(p,pc,q))return false;
    *ins=(Instruction){q[0],q[1],q[2],q[3],lbc_u32(q+4)};
    if(ins->op==MAP_NATIVE){
        PbcCall call=pbc_call(p,ins->d);
        if(call.binding<7||call.binding>11||call.argc!=1)return false;
        ins->op=(uint8_t)(MAP_SIN+call.binding-7);ins->b=call.args[0];ins->c=0;ins->d=0;
    }
    return !p->io_fault;
}
/* Returns read registers. Each operation's normal verifier also checks types. */
static int reads(Instruction i,unsigned out[3]) {
    out[0]=i.b;out[1]=i.c;out[2]=i.d;
    switch(i.op){
    case MOV:case ITOF:case FTOI:case MAP_SIN:case MAP_RED:case MAP_GREEN:case MAP_BLUE:case MAP_WHEEL:return 1;
    case IADD:case ISUB:case IMUL:case IDIV:case IMOD:
    case FADD:case FSUB:case FMUL:case FDIV:case FMOD:
    case ILT:case FLT:case EQ:case IMIN:case IMAX:case FMIN:case FMAX:case HASH:return 2;
    case RGB:case HSV:return 3;
    case MAP_STORE:out[0]=i.c;return 1;
    case JMP:case MAP_LENGTH:return 0;
    default:return -1;
    }
}
bool pbc_map_valid(Portable *p,unsigned first,unsigned end,unsigned dst,unsigned index,unsigned bound) {
    if(end<=first||end-first>32)return false;
    uint8_t defined[128]={0},captured[128]={0};defined[index]=1;
    unsigned stores=0;
    for(unsigned pc=first;pc<end;pc++){
        Instruction i;unsigned regs[3];if(!decode(p,pc,&i))return false;
        int count=reads(i,regs);if(count<0)return false;
        if(i.op==JMP){if(i.d!=pc+1)return false;continue;}
        for(int k=0;k<count;k++){
            unsigned r=regs[k];if(r>=p->nregs||pbc_type(p,r)>T_BOOL)return false;
            if(!defined[r])captured[r]=1;
        }
        if(i.op==MAP_STORE){
            if(i.a!=dst||i.b!=index||pc+1!=end)return false;
            stores++;
        }else{
            if(i.a>=p->nregs||i.a==index||i.a==bound||pbc_role(p,i.a)!=R_LOCAL||pbc_type(p,i.a)>T_BOOL)return false;
            defined[i.a]=1;
        }
    }
    for(unsigned r=0;r<p->nregs;r++)if(r!=index&&defined[r]&&captured[r])return false;
    return stores==1;
}
static bool evaluate(Instruction i,Value *v) {
    Value x=v[i.b],y=v[i.c];
    switch(i.op){
    case MOV:v[i.a]=x;break;
    case MAP_LITERAL:v[i.a].u=i.d;break;
    case IADD:v[i.a].u=x.u+y.u;break;case ISUB:v[i.a].u=x.u-y.u;break;case IMUL:v[i.a].u=x.u*y.u;break;
    case IDIV:case IMOD:{
        if(!y.i||(x.i==INT32_MIN&&y.i==-1))return false;
        int32_t div=x.i/y.i,rem=x.i%y.i;
        if(rem&&((rem<0)!=(y.i<0))){div--;rem+=y.i;}
        v[i.a].i=i.op==IDIV?div:rem;break;
    }
    case FADD:v[i.a].f=x.f+y.f;break;case FSUB:v[i.a].f=x.f-y.f;break;case FMUL:v[i.a].f=x.f*y.f;break;
    case FDIV:if(y.f==0)return false;v[i.a].f=x.f/y.f;break;
    case FMOD:if(!isfinite(x.f)||!isfinite(y.f)||y.f==0)return false;v[i.a].f=x.f-floorf(x.f/y.f)*y.f;break;
    case ITOF:v[i.a].f=(float)x.i;break;
    case FTOI:if(!isfinite(x.f)||x.f<-2147483648.0f||x.f>=2147483648.0f)return false;v[i.a].i=(int32_t)x.f;break;
    case ILT:v[i.a].u=x.i<y.i;break;case FLT:v[i.a].u=x.f<y.f;break;
    case MAP_FEQ:v[i.a].u=x.f==y.f;break;case MAP_EQ:v[i.a].u=x.u==y.u;break;
    case RGB:if(x.u>255||y.u>255||v[i.d].u>255)return false;v[i.a].u=x.u<<16|y.u<<8|v[i.d].u;break;
    case HSV:if(!isfinite(x.f)||!isfinite(y.f)||!isfinite(v[i.d].f)||y.f<0||y.f>1||v[i.d].f<0||v[i.d].f>1)return false;v[i.a].u=lbc_hsv(x.f,y.f,v[i.d].f);break;
    case IMIN:v[i.a].i=x.i<y.i?x.i:y.i;break;case IMAX:v[i.a].i=x.i>y.i?x.i:y.i;break;
    case FMIN:v[i.a].f=fminf(x.f,y.f);break;case FMAX:v[i.a].f=fmaxf(x.f,y.f);break;
    case HASH:v[i.a].f=lbc_hash01(x.i,y.i);break;
    case MAP_SIN:if(!isfinite(x.f))return false;v[i.a].f=sinf(x.f);break;
    case MAP_RED:v[i.a].u=x.u>>16;break;case MAP_GREEN:v[i.a].u=(x.u>>8)&255;break;case MAP_BLUE:v[i.a].u=x.u&255;break;
    case MAP_WHEEL:if(!isfinite(x.f)||x.f<0)return false;v[i.a].u=draw_colorwheel(x.f);break;
    default:return false;
    }
    return true;
}
typedef struct {
    Instruction code[32];Value initial[128],final[128];uint8_t cached[32];
    PbcResource *image;unsigned index,length,stride,limit;
} MapKernel;
typedef struct { MapKernel *kernel;unsigned first,end;bool success; } MapTask;
static void map_pixels(void *context){
    MapTask *task=context;MapKernel *k=task->kernel;
    Value values[128],invariants[32];memcpy(values,k->initial,sizeof(values));
    task->success=false;
    for(unsigned pixel=task->first;pixel<task->end;pixel++){
        values[k->index].u=pixel;
        for(unsigned n=0;n<k->length;n++){
            Instruction i=k->code[n];
            if(i.op==JMP)continue;
            if(i.op==MAP_STORE){
                uint32_t color=values[i.c].u;if(color>0xffffff)return;
                uint8_t *out=k->image->data+(size_t)pixel*k->stride;
                out[0]=(uint8_t)(color>>16);out[1]=(uint8_t)(color>>8);out[2]=(uint8_t)color;
                if(k->stride>3)out[3]=(k->image->kind==DRAW_IMAGE_RGBW||k->image->kind==DRAW_IMAGE_RGBWA)?0:255;
                if(k->stride>4)out[4]=255;
            }else if(k->cached[n]&&pixel>task->first)values[i.a]=invariants[n];
            else{
                if(!evaluate(i,values))return;
                if(k->cached[n])invariants[n]=values[i.a];
            }
        }
    }
    if(task->end==k->limit)memcpy(k->final,values,sizeof(values));
    task->success=true;
}
bool pbc_map_run(Portable *p,unsigned first,unsigned end,unsigned dst,unsigned index,unsigned bound) {
    PbcResource *image=&p->resources[pbc_value(p,dst).u];int32_t stop=pbc_value(p,bound).i;
    if(stop<=0){pbc_set_value(p,index,(PbcValue){0});p->bulk=0;return true;}
    if(image->sealed||!image->data||(unsigned)stop>image->count)return false;
    /* Failed transient output is discarded. Persistent resources stay serial so
       their partial writes on a fault keep the defined instruction order. */
    bool parallel=p->output&&image->data==p->output->data&&image->kind==DRAW_RGBA;
    unsigned start=p->bulk,limit=start+(parallel?128u:32u);if(limit>(unsigned)stop)limit=(unsigned)stop;
    MapKernel kernel={0};Instruction *code=kernel.code;Value *values=kernel.initial;
    uint8_t flags[128]={0},*cached=kernel.cached;flags[index]=1;
    unsigned length=end-first;
    for(unsigned n=0;n<length;n++){
        if(!decode(p,first+n,&code[n]))return false;
        Instruction *i=&code[n];unsigned regs[3];int count=reads(*i,regs);unsigned dependent=0;
        for(int k=0;k<count;k++){
            unsigned r=regs[k];dependent|=flags[r]&1u;
            if(!(flags[r]&2u)&&r!=index)flags[r]|=4u;
        }
        if(i->op!=JMP&&i->op!=MAP_STORE){
            cached[n]=(uint8_t)!dependent;flags[i->a]=(uint8_t)(2u|dependent);
            if(i->op==EQ)i->op=pbc_type(p,i->b)==T_FLOAT?MAP_FEQ:MAP_EQ;
            else if(i->op==MAP_LENGTH){i->d=p->resources[pbc_value(p,i->b).u].count;i->b=i->c=0;i->op=MAP_LITERAL;}
        }
    }
    for(unsigned r=0;r<p->nregs;r++)if(flags[r]&4u)values[r].u=pbc_value(p,r).u;
    if(p->io_fault)return false;
    kernel.image=image;kernel.index=index;kernel.length=length;
    kernel.stride=draw_stride(image->kind);kernel.limit=limit;
    unsigned middle=parallel&&limit-start>=64?start+(limit-start)/2:limit;
    MapTask tasks[2]={{&kernel,start,middle,false},{&kernel,middle,limit,false}};
    RenderJob jobs[2]={{map_pixels,&tasks[0],0},{map_pixels,&tasks[1],0}};
    unsigned count=middle<limit?2:1;render_execute(jobs,count);
    for(unsigned n=0;n<count;n++)if(!tasks[n].success)return false;
    values=kernel.final;
    for(unsigned r=0;r<p->nregs;r++)if(flags[r]&2u)pbc_set_value(p,r,(PbcValue){.u=values[r].u});
    pbc_set_value(p,index,(PbcValue){.u=limit});
    p->bulk=limit<(unsigned)stop?limit:0;p->native_pending=limit<(unsigned)stop;return true;
}
