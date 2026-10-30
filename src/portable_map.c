/* Generic execution of compiler-recognized pixel maps. No effect policies. */
#include "portable_map.h"
#include "draw.h"
#include "render_executor.h"
#include "platform.h"
#include "vm_allocator.h"
#include "noise.h"
#include <string.h>
#include <limits.h>
#include <math.h>

enum { MAP_LOAD=33, MAP_STORE=34, MAP_LENGTH=35, MAP_NATIVE=38, MAP_SIN=100, MAP_RED, MAP_GREEN, MAP_BLUE, MAP_WHEEL, MAP_FEQ, MAP_EQ, MAP_LITERAL, MAP_NOISE=112 };
typedef union { uint32_t u; int32_t i; float f; } Value;
/* Noise needs up to seven source registers. Six fit in b,c,d; the last byte
 * keeps every prepared instruction at 12 bytes, charged to the admitted heap. */
typedef struct { uint8_t op,a,b,c; uint32_t d; uint8_t extra; } Instruction;
static unsigned noise_binding(unsigned op){return op>=MAP_NOISE&&op<MAP_NOISE+4?LVM_PERLIN2+op-MAP_NOISE:0;}
static unsigned noise_reads(Instruction i,unsigned out[7]){
    out[0]=i.b;out[1]=i.c;
    for(unsigned k=0;k<4;k++)out[k+2]=(i.d>>(k*8))&255u;
    out[6]=i.extra;return lvm_noise_signature(noise_binding(i.op)).argc;
}

static bool decode(Portable *p,unsigned pc,Instruction *ins) {
    uint8_t q[8];if(!pbc_read_instruction(p,pc,q))return false;
    *ins=(Instruction){.op=q[0],.a=q[1],.b=q[2],.c=q[3],.d=lbc_u32(q+4)};
    if(ins->op==MAP_NATIVE){
        PbcCall call=pbc_call(p,ins->d);
        LvmNoiseSignature noise=lvm_noise_signature(call.binding);
        if(noise.argc){
            if(call.argc!=noise.argc)return false;
            ins->op=(uint8_t)(MAP_NOISE+call.binding-LVM_PERLIN2);
            ins->b=call.args[0];ins->c=call.args[1];ins->d=0;ins->extra=call.args[6];
            for(unsigned k=0;k<4;k++)ins->d|=(uint32_t)call.args[k+2]<<(8*k);
            return !p->io_fault;
        }
        if(call.binding<7||call.binding>11||call.argc!=1)return false;
        ins->op=(uint8_t)(MAP_SIN+call.binding-7);ins->b=call.args[0];ins->c=0;ins->d=0;
    }
    return !p->io_fault;
}
typedef struct { uint32_t word[4]; } Mask;
static bool has(Mask m,unsigned r){return (m.word[r/32]>>(r%32))&1u;}
static void add(Mask *m,unsigned r){m->word[r/32]|=1u<<(r%32);}
typedef struct { Mask written,captured,cached; unsigned first_weight,weight; bool branches; } MapInfo;
struct PbcMapPlan {
    struct PbcMapPlan *next;
    uint16_t first,end,first_weight,weight;
    uint8_t dst,index,bound;
    bool branches;
    Mask written,captured;
    Instruction code[];
};
/* Only resident numeric arrays can supply values. Identity comes from verified
 * constant references; no guest word is interpreted as a pointer. */
static int resource(Portable *p,unsigned reg){
    if(reg>=p->nregs||pbc_type(p,reg)!=6||pbc_role(p,reg)!=R_CONST)return -1;
    uint32_t id=pbc_initial(p,reg);return id<p->nresources?(int)id:-1;
}
static bool numeric_array(unsigned kind){
    return kind==1||kind==2||kind==7||kind==8||kind==11||kind==LBC_FIXED_ARRAY;
}
static bool scalar(unsigned type){return (type>=T_INT&&type<=T_BOOL)||type==T_FIXED;}
/* Normal instruction validation independently checks every exact signature. */
static int reads(Instruction i,unsigned out[7]) {
    if(noise_binding(i.op))return (int)noise_reads(i,out);
    out[0]=i.b;out[1]=i.c;out[2]=i.d;
    switch(i.op){
    case MOV:case ITOF:case FTOI:case MAP_SIN:case MAP_RED:case MAP_GREEN:case MAP_BLUE:case MAP_WHEEL:return 1;
    case IADD:case ISUB:case IMUL:case IDIV:case IMOD:
    case FADD:case FSUB:case FMUL:case FDIV:case FMOD:
    case ILT:case FLT:case EQ:case IMIN:case IMAX:case FMIN:case FMAX:case HASH:return 2;
    case RGB:case HSV:return 3;
    case MAP_LOAD:out[0]=i.c;return 1;
    case MAP_STORE:out[0]=i.c;return 1;
    case JZ:out[0]=i.a;return 1;
    case JMP:case MAP_LENGTH:return 0;
    default:{LbcNumericSignature sig=lbc_numeric_signature(i.op);return sig.result?(sig.right?2:1):-1;}
    }
}
static unsigned cost(unsigned op){
    if(noise_binding(op))return lvm_noise_signature(noise_binding(op)).work;
    return lbc_instruction_work(op==MAP_SIN?FSIN:op);
}
static bool analyze(Portable *p,unsigned first,unsigned end,unsigned dst,unsigned index,unsigned bound,MapInfo *info){
    if(end<=first||end-first>128||index>=p->nregs||bound>=p->nregs||index==bound)return false;
    *info=(MapInfo){0};unsigned length=end-first,stores=0,first_weight=0;
    for(unsigned n=0;n<length;n++){
        Instruction i;if(!decode(p,first+n,&i))return false;
        unsigned operands[7];int count=reads(i,operands);if(count<0)return false;
        for(int k=0;k<count;k++)if(operands[k]>=p->nregs||!scalar(pbc_type(p,operands[k])))return false;
        if(i.op==JMP||i.op==JZ){
            if(i.d<=first+n||i.d>=end)return false; /* No path may skip the final store. */
            if(i.op==JZ||i.d!=first+n+1)info->branches=true;
        }else if(i.op==MAP_STORE){
            if(i.a!=dst||i.b!=index||n+1!=length)return false;
            stores++;
        }else{
            if(i.a>=p->nregs||i.a==index||i.a==bound||pbc_role(p,i.a)!=R_LOCAL||!scalar(pbc_type(p,i.a)))return false;
            add(&info->written,i.a);
            if(i.op==MAP_LOAD||i.op==MAP_LENGTH){
                int id=resource(p,i.b);if(id<0)return false;
                if(i.op==MAP_LOAD){
                    if(id==resource(p,dst)||!numeric_array(pbc_spec(p,(unsigned)id).kind)||pbc_spec(p,(unsigned)id).flags)return false;
                }
            }
        }
        LbcNumericMetadata metadata=lbc_numeric_metadata(i.op);
        if(metadata.signature.result&&(!metadata.kernel||metadata.effects))return false;
    }
    if(stores!=1)return false;
    /* Forward-only CFG: intersect incoming definitions, take maximum path cost.
     * A local written anywhere in the loop cannot be captured on another path.
     * Conditional writes are retained for final-local merging between lanes. */
    Mask before[129]={0};uint16_t weight[129]={0};bool reached[129]={0};
    bool dependent[128]={0};dependent[index]=true;reached[0]=true;add(&before[0],index);
    for(unsigned n=0;n<length;n++){
        if(!reached[n])return false;
        Instruction i;unsigned operands[7];if(!decode(p,first+n,&i))return false;
        int count=reads(i,operands);bool varies=false;
        for(int k=0;k<count;k++){
            unsigned r=operands[k];varies|=dependent[r];
            if(has(info->written,r)&&!has(before[n],r))return false;
            if(!has(info->written,r)&&r!=index)add(&info->captured,r);
        }
        Mask after=before[n];unsigned work=cost(i.op);first_weight+=work;
        if(i.op!=MAP_STORE&&i.op!=JMP&&i.op!=JZ){
            add(&after,i.a);dependent[i.a]=varies;
            if(!info->branches&&!varies){add(&info->cached,n);work=1;}
        }
        unsigned targets[2]={n+1,0},targets_count=1;
        if(i.op==JMP)targets[0]=i.d-first;
        if(i.op==JZ)targets[targets_count++]=i.d-first;
        for(unsigned k=0;k<targets_count;k++){
            unsigned to=targets[k];
            if(!reached[to]){before[to]=after;reached[to]=true;}
            else for(unsigned w=0;w<4;w++)before[to].word[w]&=after.word[w];
            unsigned total=weight[n]+work;if(total>weight[to])weight[to]=(uint16_t)total;
        }
    }
    info->weight=weight[length];info->first_weight=info->branches?info->weight:first_weight;
    return info->first_weight<=LBC_MAP_WORK_BUDGET&&!p->io_fault;
}
bool pbc_map_valid(Portable *p,unsigned first,unsigned end,unsigned dst,unsigned index,unsigned bound){
    MapInfo info;return analyze(p,first,end,dst,index,bound,&info);
}
void pbc_map_destroy(Portable *p){
    while(p->maps){struct PbcMapPlan *next=p->maps->next;demo_free(p->maps);p->maps=next;}
    p->map_bytes=0;
}
int pbc_map_prepare(Portable *p,unsigned *pc,unsigned *index){
    if(*pc>=p->ncode)return 1;
    if(!*index){
        /* Bound scanning as well as plan decoding between preparation returns. */
        for(unsigned n=0;n<32&&*pc<p->ncode;n++,(*pc)++){
            uint8_t bytes[8];if(!pbc_read_instruction(p,*pc,bytes))return -1;
            if(bytes[0]!=39)continue;
            unsigned end=lbc_u32(bytes+4),length=end-*pc-1;
            if(!length||length>128)return -1;
            size_t size=sizeof(struct PbcMapPlan)+length*sizeof(Instruction);
            struct PbcMapPlan *plan=demo_alloc(size);if(!plan)return -1;
            *plan=(struct PbcMapPlan){.next=p->maps,.first=(uint16_t)(*pc+1),.end=(uint16_t)end,
                .dst=bytes[1],.index=bytes[2],.bound=bytes[3]};
            p->maps=plan;p->map_bytes+=size;*index=1;return 0;
        }
        return *pc==p->ncode?1:0;
    }
    struct PbcMapPlan *plan=p->maps;unsigned length=plan->end-plan->first;
    if(*index<=length){
        unsigned stop=*index+16;if(stop>length+1)stop=length+1;
        for(;*index<stop;(*index)++)if(!decode(p,plan->first+*index-1,&plan->code[*index-1]))return -1;
        return 0;
    }
    MapInfo info;if(!analyze(p,plan->first,plan->end,plan->dst,plan->index,plan->bound,&info))return -1;
    plan->written=info.written;plan->captured=info.captured;plan->first_weight=(uint16_t)info.first_weight;plan->weight=(uint16_t)info.weight;plan->branches=info.branches;
    for(unsigned n=0;n<length;n++){
        Instruction *i=&plan->code[n];
        if(i->op==EQ)i->op=pbc_type(p,i->b)==T_FLOAT?MAP_FEQ:MAP_EQ;
        else if(i->op==MAP_LENGTH){i->d=p->resources[pbc_initial(p,i->b)].count;i->b=i->c=0;i->op=MAP_LITERAL;}
        else if(i->op==MAP_LOAD){i->b=(uint8_t)pbc_initial(p,i->b);i->d=p->resources[i->b].kind;}
        else if(i->op==JMP||i->op==JZ)i->d-=plan->first;
        if(has(info.cached,n))i->op|=128;
    }
    *pc=plan->end;*index=0;return 0;
}
static bool evaluate(Instruction i,Value *v,const Portable *p) {
    if(noise_binding(i.op)){
        unsigned regs[7],count=noise_reads(i,regs);uint32_t args[7]={0};
        for(unsigned k=0;k<count;k++)args[k]=v[regs[k]].u;
        return lvm_noise_eval(noise_binding(i.op),args,&v[i.a].u);
    }
    Value x=v[i.b],y=v[i.c];
    switch(i.op){
    case MOV:v[i.a]=x;break;
    case MAP_LITERAL:v[i.a].u=i.d;break;
    case MAP_LOAD:{
        const PbcResource *r=&p->resources[i.b];unsigned at=v[i.c].u;
        if(at>=r->count||r->generation!=p->generation||!r->data)return false;
        v[i.a].u=0;
        if(i.d==7)v[i.a].u=r->data[at];
        else if(i.d==8){uint16_t word;memcpy(&word,r->data+2u*at,2);v[i.a].u=word;}
        else memcpy(&v[i.a].u,r->data+4u*at,4);
        break;
    }
    case IADD:v[i.a].u=x.u+y.u;break;case ISUB:v[i.a].u=x.u-y.u;break;case IMUL:v[i.a].u=x.u*y.u;break;
    case IDIV:case IMOD:{
        if(!y.i||(x.i==INT32_MIN&&y.i==-1))return false;
        int32_t div=x.i/y.i,rem=x.i%y.i;
        if(rem&&((rem<0)!=(y.i<0))){div--;rem+=y.i;}
        v[i.a].i=i.op==IDIV?div:rem;break;
    }
    case FADD:v[i.a].f=x.f+y.f;break;case FSUB:v[i.a].f=x.f-y.f;break;case FMUL:v[i.a].f=x.f*y.f;break;
    case FDIV:if(y.f==0)return false;v[i.a].f=x.f/y.f;break;
    case FMOD:if(!lbc_finite(x.f)||!lbc_finite(y.f)||y.f==0)return false;v[i.a].f=x.f-floorf(x.f/y.f)*y.f;break;
    case ITOF:v[i.a].f=(float)x.i;break;
    case FTOI:if(!lbc_finite(x.f)||x.f<-2147483648.0f||x.f>=2147483648.0f)return false;v[i.a].i=(int32_t)x.f;break;
    case ILT:v[i.a].u=x.i<y.i;break;case FLT:v[i.a].u=x.f<y.f;break;
    case MAP_FEQ:v[i.a].u=x.f==y.f;break;case MAP_EQ:v[i.a].u=x.u==y.u;break;
    case RGB:if(x.u>255||y.u>255||v[i.d].u>255)return false;v[i.a].u=x.u<<16|y.u<<8|v[i.d].u;break;
    case HSV:if(!lbc_finite(x.f)||!lbc_finite(y.f)||!lbc_finite(v[i.d].f)||y.f<0||y.f>1||v[i.d].f<0||v[i.d].f>1)return false;v[i.a].u=lbc_hsv(x.f,y.f,v[i.d].f);break;
    case IMIN:v[i.a].i=x.i<y.i?x.i:y.i;break;case IMAX:v[i.a].i=x.i>y.i?x.i:y.i;break;
    case FMIN:case FMAX:return lbc_numeric_eval(i.op,x.u,y.u,&v[i.a].u);
    case HASH:v[i.a].f=lbc_hash01(x.i,y.i);break;
    case MAP_SIN:return lbc_numeric_eval(FSIN,x.u,0,&v[i.a].u);
    case MAP_RED:v[i.a].u=x.u>>16;break;case MAP_GREEN:v[i.a].u=(x.u>>8)&255;break;case MAP_BLUE:v[i.a].u=x.u&255;break;
    case MAP_WHEEL:if(!lbc_finite(x.f)||x.f<0)return false;v[i.a].u=draw_colorwheel(x.f);break;
    default:return lbc_numeric_eval(i.op,x.u,y.u,&v[i.a].u);
    }
    return true;
}
typedef struct {
    const struct PbcMapPlan *plan;const Portable *vm;PbcResource *image;
    unsigned first,end,completed,stride,work,budget,fault_pc,fault_code;uint64_t deadline;bool success;
    Mask written;Value values[128];
} MapTask;
static LIGHTVM_DRAW_CODE void map_pixels(void *context){
    MapTask *task=context;const struct PbcMapPlan *k=task->plan;
    Value *values=task->values,invariants[128];
    unsigned length=k->end-k->first;task->success=false;task->completed=task->first;
    /* A straight-line path writes the entire verified set on every pixel.
     * Only conditional paths need per-instruction last-write tracking. */
    if(!k->branches)task->written=k->written;
    for(unsigned pixel=task->first;pixel<task->end;pixel++){
        if(pixel>task->first&&platform_time_us()>=task->deadline)break;
        /* Reserve the verified worst path before starting a pixel. Charge
         * attempts too, including faults and unpublished/recomputed suffixes. */
        unsigned work=pixel==task->first?k->first_weight:k->weight;
        if(work>task->budget-task->work){task->fault_code=PBC_FAULT_BUDGET;return;}
        task->work+=work;
        values[k->index].u=pixel;
        for(unsigned n=0;n<length;){
            task->fault_pc=k->first+n;task->fault_code=PBC_FAULT_OPERATION;
            Instruction i=k->code[n];bool cached=(i.op&128)!=0;i.op&=127;
            if(i.op==JMP){n=i.d;continue;}
            if(i.op==JZ){n=values[i.a].u?n+1:i.d;continue;}
            if(i.op==MAP_STORE){
                uint32_t color=values[i.c].u;if(color>0xffffff||pixel>=task->image->count)return;
                uint8_t *out=task->image->data+(size_t)pixel*task->stride;
                out[0]=(uint8_t)(color>>16);out[1]=(uint8_t)(color>>8);out[2]=(uint8_t)color;
                if(task->stride>3)out[3]=(task->image->kind==DRAW_IMAGE_RGBW||task->image->kind==DRAW_IMAGE_RGBWA)?0:255;
                if(task->stride>4)out[4]=255;
            }else{
                if(cached&&pixel>task->first)values[i.a]=invariants[n];
                else{
                    if(!evaluate(i,values,task->vm))return;
                    if(cached)invariants[n]=values[i.a];
                }
                if(k->branches)add(&task->written,i.a);
            }
            n++;
        }
        task->completed=pixel+1;
    }
    task->success=true;
}
bool pbc_map_run(Portable *p,unsigned first,unsigned end,unsigned dst,unsigned index,unsigned bound){
    const struct PbcMapPlan *plan=p->maps;
    while(plan&&plan->first!=first)plan=plan->next;
    if(!plan||plan->end!=end||plan->dst!=dst||plan->index!=index||plan->bound!=bound)return false;
    int32_t stop=pbc_value(p,bound).i;
    if(stop<=0){pbc_set_value(p,index,(PbcValue){0});p->bulk=0;return true;}
    PbcResource *image=&p->resources[pbc_initial(p,dst)];
    if(image->sealed||!image->data||image->generation!=p->generation)return false;
    bool transient=p->output&&image->data==p->output->data&&image->kind==DRAW_RGBA;
    bool parallel=transient&&render_executor_can_split();
    unsigned remaining=LIGHTVM_INVOCATION_LIMIT-p->used;
    /* Near the watchdog limit, use one lane so unused branch reservations do
     * not make a second worker fault while the first still has spare budget. */
    if(remaining<2u*LBC_MAP_WORK_BUDGET)parallel=false;
    unsigned budget=remaining/(parallel?2u:1u);
    if(budget>LBC_MAP_WORK_BUDGET)budget=LBC_MAP_WORK_BUDGET;
    if(budget<plan->first_weight){p->fault_code=PBC_FAULT_BUDGET;return false;}
    unsigned pixels=1u+(budget-plan->first_weight)/plan->weight;
    if(parallel)pixels*=2;
    unsigned cap=parallel?128u:transient?64u:32u;if(pixels>cap)pixels=cap;
    unsigned start=p->bulk,limit=start+pixels;if(limit>(unsigned)stop)limit=(unsigned)stop;
    unsigned middle=parallel&&limit-start>=2?start+(limit-start)/2:limit;
    unsigned count=middle<limit?2:1;
    MapTask tasks[2]={0};uint64_t deadline=platform_time_us()+LIGHTVM_RENDER_SLICE_US;
    for(unsigned n=0;n<count;n++){
        tasks[n].plan=plan;tasks[n].vm=p;tasks[n].image=image;tasks[n].stride=draw_stride(image->kind);
        tasks[n].first=n?middle:start;tasks[n].end=n?limit:middle;tasks[n].deadline=deadline;tasks[n].budget=budget;
        for(unsigned r=0;r<p->nregs;r++)if(has(plan->captured,r))tasks[n].values[r].u=pbc_value(p,r).u;
    }
    if(p->io_fault)return false;
    RenderJob jobs[2]={{map_pixels,&tasks[0],0},{map_pixels,&tasks[1],0}};render_execute(jobs,count);
    bool success=true;
    for(unsigned n=0;n<count;n++){p->used+=tasks[n].work;success&=tasks[n].success;}
    if(!success){
        for(unsigned n=0;n<count;n++)if(!tasks[n].success){p->fault_pc=tasks[n].fault_pc;p->fault_code=tasks[n].fault_code;break;}
        return false;
    }
    /* Only the contiguous completed prefix advances. A speculative transient
     * suffix is unpublished and will be recomputed if the first lane yielded. */
    if(tasks[0].completed<middle)count=1;
    unsigned completed=tasks[count-1].completed;
    for(unsigned r=0;r<p->nregs;r++)if(has(plan->written,r)){
        for(unsigned n=count;n;n--)if(has(tasks[n-1].written,r)){
            pbc_set_value(p,r,(PbcValue){.u=tasks[n-1].values[r].u});break;
        }
    }
    pbc_set_value(p,index,(PbcValue){.u=completed});
    p->bulk=completed<(unsigned)stop?completed:0;p->native_pending=completed<(unsigned)stop;return true;
}
