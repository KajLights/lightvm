#include "portable.h"
#include "vm_allocator.h"
#include <math.h>
#include <limits.h>
#include <string.h>

/* Opcodes 0..32 retain their numeric assignments, but v3 functions return to
 * their native caller; profile scheduling never lives in the interpreter. */
enum { P_STAMP=5,P_REF=6,P_SETTING=6,P_LOAD=33,P_STORE,P_LEN,P_FILL,P_COPY,P_NATIVE };
enum { WRITE_STATE=1,WRITE_FRAME=2,WRITE_OUTPUT=4,USE_RNG=8,ANIMATION_EVENT=16 };
typedef struct { uint8_t result,argc,args[4],rights; } NativeSignature;
static const NativeSignature natives[]={
    {0,0,{0},0}, {P_STAMP,0,{0},0}, {T_FLOAT,1,{P_STAMP},0},
    {T_FLOAT,1,{P_REF},USE_RNG}, {T_INT,2,{P_REF,T_INT},USE_RNG},
    {0,1,{T_INT},ANIMATION_EVENT}, {0,0,{0},ANIMATION_EVENT},
    {T_FLOAT,1,{T_FLOAT},0}, {T_INT,1,{T_COLOR},0},
    {T_INT,1,{T_COLOR},0}, {T_INT,1,{T_COLOR},0},
    {T_COLOR,1,{T_FLOAT},0}, {T_COLOR,1,{T_INT},0},
    {T_FLOAT,1,{T_COLOR},0}, {0,1,{T_BOOL},0},
};
unsigned pbc_type(const Portable *p,unsigned r){return r<p->nregs?p->descriptors[r*8]:0;}
unsigned pbc_role(const Portable *p,unsigned r){return r<p->nregs?p->descriptors[r*8+1]:UINT_MAX;}
uint32_t pbc_initial(const Portable *p,unsigned r){return lbc_u32(p->descriptors+r*8+4);}
static bool finite_value(unsigned type,uint32_t value){
    return type==T_FLOAT ? (value&0x7f800000u)!=0x7f800000u :
        type==T_BOOL ? value<2 : type==T_COLOR ? value<=0xffffff : type==P_STAMP ? !value : true;
}
static bool read_at(Portable *p,uint32_t offset,void *out,size_t size){
    return offset<=p->size && size<=p->size-offset && p->read(p->context,offset,out,size);
}
static bool instruction(Portable *p,unsigned pc,uint8_t out[8]){
    uint32_t offset=p->code_offset+pc*8;
    if(offset<p->cache_offset || offset+8>p->cache_offset+p->cache_size){
        p->cache_offset=offset&~255u;p->cache_size=p->size-p->cache_offset;
        if(p->cache_size>256)p->cache_size=256;
        if(!read_at(p,p->cache_offset,p->cache,p->cache_size)){p->cache_size=0;return false;}
    }
    memcpy(out,p->cache+(offset-p->cache_offset),8);return true;
}
static unsigned element_type(unsigned kind){return kind==7||kind==8?T_INT:kind==10?T_COLOR:kind;}
static unsigned element_size(unsigned kind){
    switch(kind){case 1:case 2:return 4;case 3:return 3;case 4:case 7:return 1;case 5:return 8;case 8:return 2;default:return 0;}
}
static int resource_id(const Portable *p,unsigned reg){
    if(pbc_type(p,reg)!=P_REF || pbc_role(p,reg)!=R_CONST)return -1;
    uint32_t id=pbc_initial(p,reg);return id<p->nresources?(int)id:-1;
}
static bool can_write(const Portable *p,unsigned reg,unsigned rights){
    unsigned role=pbc_role(p,reg);
    return role==R_LOCAL || (role==R_STATE&&(rights&WRITE_STATE)) || (role==R_FRAME&&(rights&WRITE_FRAME));
}
static bool resource_write(const Portable *p,unsigned reg,unsigned rights){
    int id=resource_id(p,reg);
    return id>=0 && element_size(p->specs[id].kind) && (rights&(p->specs[id].flags?WRITE_OUTPUT:WRITE_STATE));
}
static bool valid_instruction(Portable *p,unsigned pc,const PbcFunction *fn){
    uint8_t q[8];if(!instruction(p,pc,q))return false;
    unsigned op=q[0],a=q[1],b=q[2],c=q[3],at=pbc_type(p,a),bt=pbc_type(p,b),ct=pbc_type(p,c);
    uint32_t d=lbc_u32(q+4);
    if(pc+1==fn->end && op!=(fn->result?RETURN:END))return false;
    if(op==END)return !fn->result&&!a&&!b&&!c&&!d;
    if(op==RETURN)return fn->result && at==fn->result&&!b&&!c&&!d;
    if(op==JMP||op==JZ)return (op==JMP?!a:at==T_BOOL)&&!b&&!c&&d>=fn->first&&d<fn->end;
    if(op==P_STORE){int id=resource_id(p,a);return resource_write(p,a,fn->rights)&&bt==T_INT&&ct==element_type(p->specs[id].kind)&&!d;}
    if(op==P_FILL){int id=resource_id(p,a);return resource_write(p,a,fn->rights)&&bt==element_type(p->specs[id].kind)&&ct==T_INT&&pbc_type(p,d)==T_INT;}
    if(op==P_COPY){int x=resource_id(p,a),y=resource_id(p,b);return resource_write(p,a,fn->rights)&&y>=0&&(!p->specs[y].flags||(fn->rights&WRITE_OUTPUT))&&p->specs[x].kind==(p->specs[y].kind==10?3:p->specs[y].kind)&&!c&&!d;}
    if(op==P_NATIVE){
        if(b||c||d>=p->ncalls)return false;
        const PbcCall *site=&p->calls[d];if(!site->binding||site->binding>=sizeof(natives)/sizeof(*natives))return false;
        const NativeSignature *sig=&natives[site->binding];
        if(site->reserved||site->argc!=sig->argc||(sig->rights&~fn->rights))return false;
        for(unsigned n=0;n<4;n++)if(n<site->argc?pbc_type(p,site->args[n])!=sig->args[n]:site->args[n]!=0)return false;
        if(site->binding==3||site->binding==4){int id=resource_id(p,site->args[0]);if(id<0||p->specs[id].kind!=9)return false;}
        return sig->result ? can_write(p,a,fn->rights)&&at==sig->result : a==255;
    }
    if(!can_write(p,a,fn->rights))return false;
    switch(op){
    case MOV:return at==bt&&!c&&!d;
    case IADD:case ISUB:case IMUL:case IDIV:case IMOD:case IMIN:case IMAX:return at==T_INT&&bt==T_INT&&ct==T_INT&&!d;
    case FADD:case FSUB:case FMUL:case FDIV:case FMOD:case FMIN:case FMAX:return at==T_FLOAT&&bt==T_FLOAT&&ct==T_FLOAT&&!d;
    case ITOF:return at==T_FLOAT&&bt==T_INT&&!c&&!d;
    case FTOI:return at==T_INT&&bt==T_FLOAT&&!c&&!d;
    case ILT:return at==T_BOOL&&bt==T_INT&&ct==T_INT&&!d;
    case FLT:return at==T_BOOL&&bt==T_FLOAT&&ct==T_FLOAT&&!d;
    case EQ:return at==T_BOOL&&bt&&bt!=P_REF&&bt==ct&&!d;
    case RGB:return at==T_COLOR&&bt==T_INT&&ct==T_INT&&pbc_type(p,d)==T_INT;
    case HSV:return at==T_COLOR&&bt==T_FLOAT&&ct==T_FLOAT&&pbc_type(p,d)==T_FLOAT;
    case HASH:return at==T_FLOAT&&bt==T_INT&&ct==T_INT&&!d;
    case P_LOAD:{int id=resource_id(p,b);return id>=0&&(!p->specs[id].flags||(fn->rights&WRITE_OUTPUT))&&p->specs[id].kind!=9&&at==element_type(p->specs[id].kind)&&ct==T_INT&&!d;}
    case P_LEN:{int id=resource_id(p,b);return id>=0&&p->specs[id].kind!=9&&at==T_INT&&!c&&!d;}
    default:return false;
    }
}
static bool valid_expressions(Portable *p){
    uint32_t dependencies[128]={0};int setting_for_reg[128];
    for(unsigned r=0;r<128;r++)setting_for_reg[r]=-1;
    for(unsigned n=0;n<p->nsettings;n++){
        PbcSetting *s=&p->settings[n];
        if(s->reg>=p->nregs||pbc_role(p,s->reg)!=P_SETTING||setting_for_reg[s->reg]>=0||s->initial>=p->nexpressions||
            (s->minimum!=UINT32_MAX&&s->minimum>=p->nexpressions)||(s->maximum!=UINT32_MAX&&s->maximum>=p->nexpressions))return false;
        setting_for_reg[s->reg]=(int)n;
    }
    for(unsigned r=0;r<p->nregs;r++)if(pbc_role(p,r)==P_SETTING&&setting_for_reg[r]<0)return false;
    for(unsigned n=0;n<p->nexpressions;n++){
        PbcExpression *e=&p->expressions[n];
        if(e->reserved||e->type<T_INT||e->type>T_BOOL)return false;
        if(e->op==0){if(e->a||e->b||!finite_value(e->type,e->value))return false;}
        else if(e->op==1){if(e->type!=T_INT||e->a||e->b||e->value)return false;}
        else if(e->op==2){
            if(e->a>=p->nregs||setting_for_reg[e->a]<0||e->type!=pbc_type(p,e->a)||e->b||e->value)return false;
            dependencies[n]=1u<<setting_for_reg[e->a];
        }else{
            if(e->op>10||e->a>=n||e->b>=n||e->value)return false;
            unsigned a=p->expressions[e->a].type,b=p->expressions[e->b].type;
            if((a!=T_INT&&a!=T_FLOAT)||(b!=T_INT&&b!=T_FLOAT))return false;
            unsigned expected=a==T_FLOAT||b==T_FLOAT||e->op==8?T_FLOAT:T_INT;
            if(e->type!=expected||(e->op==6&&expected!=T_INT))return false;
            dependencies[n]=dependencies[e->a]|dependencies[e->b];
        }
    }
    for(unsigned n=0;n<p->nsettings;n++){
        PbcSetting *s=&p->settings[n];unsigned type=pbc_type(p,s->reg);uint32_t allowed=(1u<<n)-1;
        if(type!=p->expressions[s->initial].type||(dependencies[s->initial]&~allowed))return false;
        uint32_t bounds[2]={s->minimum,s->maximum};
        for(unsigned k=0;k<2;k++)if(bounds[k]!=UINT32_MAX){
            unsigned t=p->expressions[bounds[k]].type;
            if((type!=T_INT&&type!=T_FLOAT)||(t!=type&&!(type==T_FLOAT&&t==T_INT))||(dependencies[bounds[k]]&~allowed))return false;
        }
    }
    return true;
}
void pbc_destroy(Portable *p){if(p){demo_free(p->state);demo_free(p->metadata);demo_free(p);}}
const char *pbc_open(Portable **out,LbcRead read,void *context,size_t size){
    *out=NULL;uint8_t h[96];
    if(size<96||size>32768||!read(context,0,h,96)||memcmp(h,"LBC1",4)||lbc_u32(h+4)!=3)return "invalid portable header";
    for(unsigned n=80;n<96;n+=4)if(lbc_u32(h+n))return "reserved portable header field";
    Portable *p=demo_calloc(1,sizeof(*p));if(!p)return "portable context exceeds memory budget";
    p->read=read;p->context=context;p->size=(uint32_t)size;
    p->nregs=lbc_u32(h+8);p->ncode=lbc_u32(h+12);p->ncolors=lbc_u32(h+16);
    p->nfunctions=lbc_u32(h+20);p->nresources=lbc_u32(h+24);p->nexpressions=lbc_u32(h+28);
    p->ncalls=lbc_u32(h+32);p->nsettings=lbc_u32(h+36);
    for(unsigned n=0;n<4;n++)p->entries[n]=lbc_u32(h+40+4*n);
    p->mode=lbc_u32(h+56);p->period_expression=lbc_u32(h+60);p->max_catch_up=lbc_u32(h+64);
    p->first=lbc_u32(h+68);p->profile=lbc_u32(h+72);p->manifest_size=lbc_u32(h+76);
    const char *error="invalid portable limits or profile";
    if(!p->nregs||p->nregs>128||!p->ncode||p->ncode>2048||p->ncolors>1024||!p->nfunctions||p->nfunctions>16||
        p->nresources>16||!p->nexpressions||p->nexpressions>128||p->ncalls>256||p->nsettings>16||!p->manifest_size||p->manifest_size>8192)goto fail;
    p->code_offset=96+p->nregs*8;p->palette_offset=p->code_offset+p->ncode*8;
    uint32_t table=p->palette_offset+p->ncolors*4;
    p->metadata_bytes=p->nregs*8+p->nfunctions*24+p->nresources*24+p->nexpressions*16+p->ncalls*16+p->nsettings*16;
    p->manifest_offset=table+(uint32_t)p->metadata_bytes-p->nregs*8;
    if(p->manifest_offset+p->manifest_size!=size)goto fail;
    p->metadata=demo_calloc(1,p->metadata_bytes);if(!p->metadata){error="portable metadata exceeds memory budget";goto fail;}
    p->descriptors=p->metadata;
    uint8_t *cursor=p->metadata+p->nregs*8;
    p->functions=(PbcFunction*)cursor;cursor+=p->nfunctions*24;
    p->specs=(PbcResourceSpec*)cursor;cursor+=p->nresources*24;
    p->expressions=(PbcExpression*)cursor;cursor+=p->nexpressions*16;
    p->calls=(PbcCall*)cursor;cursor+=p->ncalls*16;p->settings=(PbcSetting*)cursor;
    if(!read_at(p,96,p->descriptors,p->nregs*8))goto fail;
    for(unsigned r=0;r<p->nregs;r++){
        unsigned type=pbc_type(p,r),role=pbc_role(p,r);uint32_t value=pbc_initial(p,r);
        if(!type||type>P_REF||role>P_SETTING||p->descriptors[r*8+2]||p->descriptors[r*8+3]||!finite_value(type,value))goto fail;
        if(type==P_REF&&((role==R_CONST&&value>=p->nresources)||(role!=R_CONST&&role!=R_INPUT&&role!=R_LOCAL)||(role!=R_CONST&&value)))goto fail;
        if(role==P_SETTING&&(type>T_BOOL||value))goto fail;
    }
    /* Decode table words explicitly: the bytecode is little endian on all hosts. */
    for(unsigned n=0;n<p->nfunctions;n++,table+=24){uint8_t q[24];if(!read_at(p,table,q,24))goto fail;
        p->functions[n]=(PbcFunction){lbc_u32(q),lbc_u32(q+4),lbc_u32(q+8),lbc_u32(q+12),lbc_u32(q+16),lbc_u32(q+20)};}
    for(unsigned n=0;n<p->nresources;n++,table+=24){uint8_t q[24];if(!read_at(p,table,q,24))goto fail;
        p->specs[n]=(PbcResourceSpec){lbc_u32(q),lbc_u32(q+4),lbc_u32(q+8),lbc_u32(q+12),lbc_u32(q+16),lbc_u32(q+20)};}
    for(unsigned n=0;n<p->nexpressions;n++,table+=16){uint8_t q[16];if(!read_at(p,table,q,16))goto fail;
        p->expressions[n]=(PbcExpression){q[0],q[1],(uint16_t)(q[2]|q[3]<<8),lbc_u32(q+4),lbc_u32(q+8),lbc_u32(q+12)};}
    for(unsigned n=0;n<p->ncalls;n++,table+=16){uint8_t q[16];if(!read_at(p,table,q,16))goto fail;
        p->calls[n]=(PbcCall){lbc_u32(q),lbc_u32(q+4),{q[8],q[9],q[10],q[11]},lbc_u32(q+12)};}
    for(unsigned n=0;n<p->ncalls;n++){
        PbcCall *call=&p->calls[n];
        if(!call->binding||call->binding>=sizeof(natives)/sizeof(*natives)||call->reserved){error="invalid native binding";goto fail;}
        const NativeSignature *sig=&natives[call->binding];
        if(call->argc!=sig->argc)goto fail;
        for(unsigned k=0;k<4;k++)if(k<call->argc?pbc_type(p,call->args[k])!=sig->args[k]:call->args[k]!=0)goto fail;
    }
    for(unsigned n=0;n<p->nsettings;n++,table+=16){uint8_t q[16];if(!read_at(p,table,q,16))goto fail;
        p->settings[n]=(PbcSetting){lbc_u32(q),lbc_u32(q+4),lbc_u32(q+8),lbc_u32(q+12)};}
    error="invalid portable declarations";
    if(!valid_expressions(p))goto fail;
    for(unsigned n=0;n<p->nresources;n++){
        PbcResourceSpec *s=&p->specs[n];
        if(!s->kind||s->kind==6||s->kind>10||s->flags>1||s->length>=p->nexpressions||s->fill>=p->nexpressions||s->seed>=p->nexpressions||s->stream>=p->nexpressions)goto fail;
        if(p->expressions[s->length].type!=T_INT||p->expressions[s->seed].type!=T_INT||p->expressions[s->stream].type!=T_INT)goto fail;
        unsigned fill_type=p->expressions[s->fill].type;
        if(s->kind!=9 && fill_type!=(s->kind==5||s->kind==10?T_INT:element_type(s->kind)))goto fail;
    }
    unsigned previous=0;
    for(unsigned n=0;n<p->nfunctions;n++){
        PbcFunction *fn=&p->functions[n];
        if(fn->first!=previous||fn->end<=fn->first||fn->end>p->ncode||fn->rights>31||fn->result>P_REF||fn->arg_count>8||fn->arg_start>p->nregs||fn->arg_count>p->nregs-fn->arg_start)goto fail;
        for(unsigned k=0;k<fn->arg_count;k++)if(pbc_role(p,fn->arg_start+k)!=R_INPUT)goto fail;
        previous=fn->end;
        for(unsigned pc=fn->first;pc<fn->end;pc++)if(!valid_instruction(p,pc,fn)){error="invalid portable instruction or effect permission";goto fail;}
    }
    if(previous!=p->ncode)goto fail;
    for(unsigned n=0;n<p->ncolors;n++){uint8_t q[4];if(!read_at(p,p->palette_offset+4*n,q,4)||lbc_u32(q)>0xffffff)goto fail;}
    p->status=1;*out=p;return NULL;
fail:pbc_destroy(p);return error;
}
static double number(PbcValue value,unsigned type){return type==T_FLOAT?value.f:value.i;}
static bool evaluate(Portable *p,unsigned id,unsigned count,const PbcValue *bindings,PbcValue *out){
    PbcValue values[128]={0};
    for(unsigned n=0;n<=id;n++){
        PbcExpression *e=&p->expressions[n];PbcValue v={0};
        if(e->op==0)v.u=e->value;
        else if(e->op==1)v.i=(int32_t)count;
        else if(e->op==2)v=bindings[e->a];
        else if(e->type==T_INT){
            int64_t a=values[e->a].i,b=values[e->b].i,result;
            switch(e->op){
            case 3:result=a+b;break;case 4:result=a-b;break;case 5:result=a*b;break;
            case 6:case 7:if(!b)return false;result=a/b;if(a%b&&((a%b<0)!=(b<0)))result--;if(e->op==7)result=a-result*b;break;
            case 9:result=a<b?a:b;break;case 10:result=a>b?a:b;break;default:return false;}
            if(result<INT32_MIN||result>INT32_MAX)return false;
            v.i=(int32_t)result;
        }else{
            float a=(float)number(values[e->a],p->expressions[e->a].type),b=(float)number(values[e->b],p->expressions[e->b].type);
            switch(e->op){case 3:v.f=a+b;break;case 4:v.f=a-b;break;case 5:v.f=a*b;break;
            case 8:if(b==0)return false;v.f=a/b;break;case 7:if(b==0)return false;v.f=a-floorf(a/b)*b;break;
            case 9:v.f=fminf(a,b);break;case 10:v.f=fmaxf(a,b);break;default:return false;}
            if(!isfinite(v.f))return false;
        }
        values[n]=v;
    }
    *out=values[id];return true;
}
static bool store_value(PbcResource *r,unsigned index,PbcValue value){
    if(index>=r->count||r->sealed||!r->data)return false;
    uint8_t *at=r->data+index*element_size(r->kind);
    switch(r->kind){
    case 1:memcpy(at,&value.i,4);break;
    case 2:if(!isfinite(value.f))return false;memcpy(at,&value.f,4);break;
    case 3:if(value.u>0xffffff)return false;at[0]=(uint8_t)(value.u>>16);at[1]=(uint8_t)(value.u>>8);at[2]=(uint8_t)value.u;break;
    case 4:if(value.u>1)return false;*at=(uint8_t)value.u;break;
    case 5:memcpy(at,&value.stamp,8);break;
    case 7:if(value.u>255)return false;*at=(uint8_t)value.u;break;
    case 8:if(value.u>65535)return false;{uint16_t v=(uint16_t)value.u;memcpy(at,&v,2);}break;
    default:return false;
    }
    return true;
}
static bool load_value(Portable *p,PbcResource *r,unsigned index,PbcValue *value){
    if(index>=r->count)return false;
    memset(value,0,sizeof(*value));
    if(r->kind==10){uint8_t q[4];if(!read_at(p,p->palette_offset+4*(r->random_state+index),q,4))return false;value->u=lbc_u32(q);return true;}
    const uint8_t *at=r->data+index*element_size(r->kind);
    switch(r->kind){
    case 1:memcpy(&value->i,at,4);break;case 2:memcpy(&value->f,at,4);break;
    case 3:value->u=(uint32_t)at[0]<<16|(uint32_t)at[1]<<8|at[2];break;
    case 4:case 7:value->u=*at;break;case 5:memcpy(&value->stamp,at,8);break;
    case 8:{uint16_t v;memcpy(&v,at,2);value->u=v;}break;default:return false;
    }return true;
}
static bool resolve_settings(Portable *p,unsigned count,PbcValue *bindings){
    for(unsigned n=0;n<p->nsettings;n++){
        PbcSetting *s=&p->settings[n];PbcValue value,lo,hi;
        if(!evaluate(p,s->initial,count,bindings,&value))return false;
        if(p->override_mask&(1u<<n))value=p->overrides[n];
        unsigned typ=pbc_type(p,s->reg);double v=number(value,typ);
        if(s->minimum!=UINT32_MAX&&(!evaluate(p,s->minimum,count,bindings,&lo)||v<number(lo,p->expressions[s->minimum].type)))return false;
        if(s->maximum!=UINT32_MAX&&(!evaluate(p,s->maximum,count,bindings,&hi)||v>number(hi,p->expressions[s->maximum].type)))return false;
        bindings[s->reg]=value;
    }
    return true;
}
const char *pbc_period(Portable *p,unsigned count,uint64_t *period_us){
    if(p->initialized){*period_us=p->period_us;return NULL;}
    PbcValue bindings[128]={0},period;
    for(unsigned r=0;r<p->nregs;r++)bindings[r].u=pbc_initial(p,r);
    if(!resolve_settings(p,count,bindings)||p->period_expression>=p->nexpressions||
       p->expressions[p->period_expression].type!=T_INT||
       !evaluate(p,p->period_expression,count,bindings,&period)||period.i<0||
       (p->period_expression&&period.i<1000)||period.i>1000000)return "invalid setting or period expression";
    *period_us=(uint32_t)period.i;return NULL;
}
const char *pbc_prepare(Portable *p,LbcBuffer *output){
    if(p->initialized||p->state||(output&&(!output->data||!output->count||output->count>65535||output->sealed)))return "invalid resource preparation";
    unsigned count=output?output->count:0;
    p->state_bytes=p->nregs*sizeof(PbcValue)+p->nresources*sizeof(PbcResource);
    p->state=demo_calloc(1,p->state_bytes);if(!p->state)return "portable state exceeds memory budget";
    p->values=p->state;p->resources=(PbcResource*)(p->values+p->nregs);p->generation=output?output->generation:0;p->output=output;
    for(unsigned r=0;r<p->nregs;r++)p->values[r].u=pbc_initial(p,r);
    const char *error="invalid setting or resource expression";
    if(!resolve_settings(p,count,p->values))goto fail;
    size_t payload=0;
    for(unsigned n=0;n<p->nresources;n++){
        PbcResourceSpec *s=&p->specs[n];PbcResource *r=&p->resources[n];PbcValue length,fill,seed,stream;
        if(!evaluate(p,s->length,count,p->values,&length)||length.i<0||length.i>65535||!evaluate(p,s->fill,count,p->values,&fill))goto fail;
        r->kind=s->kind;r->count=(uint32_t)length.i;r->generation=p->generation;
        if(s->flags){
            if(!output||s->kind!=3||r->count!=output->count)goto fail;
            r->data=output->data;continue;
        }
        if(s->kind==9){
            if(!evaluate(p,s->seed,count,p->values,&seed)||!evaluate(p,s->stream,count,p->values,&stream)||seed.i<0||stream.i<0)goto fail;
            r->random_state=seed.u^(stream.u*0x9e3779b9u);if(!r->random_state)r->random_state=0x6d2b79f5u;
        }else if(s->kind==10){
            if(fill.u>p->ncolors||r->count>p->ncolors-fill.u)goto fail;
            r->random_state=fill.u;r->sealed=true;
        }else{
            if(s->kind==5&&fill.u)goto fail;
            size_t bytes=r->count*element_size(r->kind);
            if(bytes>VM_HEAP_LIMIT-payload)goto fail;
            payload+=bytes;
        }
    }
    /* Reallocate once at admission to include payload; no execution-time heap. */
    if(payload){
        void *larger=demo_calloc(1,p->state_bytes+payload);if(!larger){error="portable resources exceed peak memory budget";goto fail;}
        memcpy(larger,p->state,p->state_bytes);demo_free(p->state);p->state=larger;
        p->values=p->state;p->resources=(PbcResource*)(p->values+p->nregs);
    }
    p->payload_bytes=payload;uint8_t *cursor=(uint8_t*)p->state+p->state_bytes;p->state_bytes+=payload;
    for(unsigned n=0;n<p->nresources;n++){
        PbcResource *r=&p->resources[n];if(p->specs[n].flags||!element_size(r->kind))continue;
        r->data=cursor;cursor+=r->count*element_size(r->kind);PbcValue fill;
        if(!evaluate(p,p->specs[n].fill,count,p->values,&fill))goto fail;
        for(unsigned k=0;k<r->count;k++)if(!store_value(r,k,fill))goto fail;
    }
    p->initialized=true;return NULL;
fail:demo_free(p->state);p->state=NULL;p->values=NULL;p->resources=NULL;p->state_bytes=0;return error;
}
bool pbc_parameter(Portable *p,unsigned reg,uint32_t bits){
    if(!p->initialized||p->status==0||reg>=p->nregs||pbc_role(p,reg)!=R_PARAM||!finite_value(pbc_type(p,reg),bits))return false;
    p->values[reg].u=bits;return true;
}
bool pbc_invoke(Portable *p,unsigned function,const PbcValue *arguments,unsigned count){
    if(!p->initialized||p->status==0||function>=p->nfunctions)return false;
    const PbcFunction *fn=&p->functions[function];
    if(count!=fn->arg_count||(count&&!arguments))return false;
    for(unsigned n=0;n<count;n++){
        unsigned type=pbc_type(p,fn->arg_start+n);
        if((type!=P_STAMP&&!finite_value(type,arguments[n].u))||(type==P_REF&&arguments[n].u>=p->nresources))return false;
    }
    for(unsigned r=0;r<p->nregs;r++)if(pbc_role(p,r)==R_LOCAL){
        p->values[r].stamp=0;p->values[r].u=pbc_initial(p,r);
    }
    for(unsigned n=0;n<count;n++)p->values[fn->arg_start+n]=arguments[n];
    p->function=function;p->pc=fn->first;p->used=0;p->bulk=0;p->status=0;return true;
}
static uint32_t random_word(void *context){
    PbcResource *r=context;
    uint32_t x=r->random_state;x^=x<<13;x^=x>>17;x^=x<<5;return r->random_state=x;
}
bool pbc_random_index(uint32_t bound,uint32_t (*word)(void *),void *context,int32_t *result){
    if(!bound||bound>65535||!word||!result)return false;
    uint64_t limit=UINT64_C(4294967296)-(UINT64_C(4294967296)%bound);
    for(unsigned n=0;n<4;n++){uint32_t x=word(context);if(x<limit){*result=(int32_t)(x%bound);return true;}}
    *result=-1;return true;
}
/* Native signatures and permission checks above are shared for every binding.
 * Bodies below implement library versions, not compiler-specific AST behaviour. */
static bool native_call(Portable *p,const PbcCall *site,PbcValue *result){
    PbcValue a[4]={{0}};for(unsigned k=0;k<site->argc;k++)a[k]=p->values[site->args[k]];
    switch(site->binding){
    case 1:result->stamp=p->now_us;return true;
    case 2:if(a[0].stamp>p->now_us)return false;result->f=(float)((p->now_us-a[0].stamp)/1000000.0);return true;
    case 3:result->f=(random_word(&p->resources[a[0].u])>>8)*0x1p-24f;return true;
    case 4:{
        if(!pbc_random_index(a[1].u,random_word,&p->resources[a[0].u],&result->i))return false;
        if(result->i>=0)return true;
        if(p->random_exhaustions==UINT64_MAX)return false;
        p->random_exhaustions++;result->i=-1;return true;
    }
    case 5:if(a[0].i<=0||UINT64_MAX-p->pending_cycles<a[0].u)return false;p->pending_cycles+=a[0].u;return true;
    case 6:p->completion_pending=true;return true;
    case 7:if(!isfinite(a[0].f))return false;result->f=sinf(a[0].f);return true;
    case 8:result->i=(int32_t)(a[0].u>>16);return true;
    case 9:result->i=(int32_t)((a[0].u>>8)&255);return true;
    case 10:result->i=(int32_t)(a[0].u&255);return true;
    case 11:{
        if(!isfinite(a[0].f)||a[0].f<0)return false;
        float hue=fmodf(a[0].f,256.0f);unsigned sector=hue<85?0:hue<170?1:2;
        float part=hue-(float)(sector*85);unsigned rising=(unsigned)(part*3);if(rising>255)rising=255;
        unsigned falling=255-rising;
        result->u=sector==0?(falling<<16|rising<<8):sector==1?(falling<<8|rising):(rising<<16|falling);return true;
    }
    case 12:{
        if(a[0].i<0||a[0].i>360)return false;
        unsigned hue=a[0].u%360,sector=hue/120;double angle=(hue%120)*(3.14159265358979323846/240.0);
        unsigned x=(unsigned)(255*cos(angle)),y=(unsigned)(255*sin(angle));
        result->u=sector==0?(x<<16|y<<8):sector==1?(x<<8|y):(y<<16|x);return true;
    }
    case 13:{
        double r=(a[0].u>>16)/255.0,g=((a[0].u>>8)&255)/255.0,b=(a[0].u&255)/255.0;
        double top=fmax(r,fmax(g,b)),bottom=fmin(r,fmin(g,b)),d=top-bottom,h=0;
        if(d){if(top==r){h=fmod((g-b)/d,6.0);if(h<0)h+=6.0;h*=60.0;}else if(top==g)h=60.0*((b-r)/d+2.0);else h=60.0*((r-g)/d+4.0);}
        double low=floor(h),fraction=h-low;result->f=(float)(low+(fraction>0.5||(fraction==0.5&&fmod(low,2.0)!=0)));return true;
    }
    case 14:return a[0].u!=0;
    default:return false;
    }
}
int pbc_resume(Portable *p,unsigned quota){
    if(p->status)return p->status==1?1:-1;
    for(unsigned n=0;n<p->nresources;n++)if(p->resources[n].generation!=p->generation){p->status=UINT32_MAX;return -1;}
    if((p->output&&(p->output->sealed||p->output->generation!=p->generation))||p->used>=2000000){p->status=UINT32_MAX;return -1;}
    if(quota>2000000-p->used)quota=2000000-p->used;
    PbcValue *r=p->values;
    while(quota--){
        uint8_t q[8];if(!instruction(p,p->pc,q))goto fault;
        unsigned op=q[0],a=q[1],b=q[2],c=q[3];uint32_t d=lbc_u32(q+4);p->pc++;p->used++;
        switch(op){
        case END:p->status=1;return 1;
        case RETURN:p->result=r[a];p->status=1;return 1;
        case MOV:r[a]=r[b];break;
        case IADD:r[a].u=r[b].u+r[c].u;break;case ISUB:r[a].u=r[b].u-r[c].u;break;case IMUL:r[a].u=r[b].u*r[c].u;break;
        case IDIV:case IMOD:{int32_t x=r[b].i,y=r[c].i;if(!y||(x==INT32_MIN&&y==-1))goto fault;int32_t div=x/y,rem=x%y;if(rem&&((rem<0)!=(y<0))){div--;rem+=y;}r[a].i=op==IDIV?div:rem;break;}
        case FADD:r[a].f=r[b].f+r[c].f;break;case FSUB:r[a].f=r[b].f-r[c].f;break;case FMUL:r[a].f=r[b].f*r[c].f;break;
        case FDIV:if(r[c].f==0)goto fault;r[a].f=r[b].f/r[c].f;break;
        case FMOD:if(!isfinite(r[b].f)||!isfinite(r[c].f)||r[c].f==0)goto fault;r[a].f=r[b].f-floorf(r[b].f/r[c].f)*r[c].f;break;
        case ITOF:r[a].f=(float)r[b].i;break;
        case FTOI:if(!isfinite(r[b].f)||r[b].f<-2147483648.0f||r[b].f>=2147483648.0f)goto fault;r[a].i=(int32_t)r[b].f;break;
        case ILT:r[a].u=r[b].i<r[c].i;break;case FLT:r[a].u=r[b].f<r[c].f;break;
        case EQ:r[a].u=pbc_type(p,b)==T_FLOAT?r[b].f==r[c].f:pbc_type(p,b)==P_STAMP?r[b].stamp==r[c].stamp:r[b].u==r[c].u;break;
        case JZ:if(!r[a].u)p->pc=d;break;case JMP:p->pc=d;break;
        case RGB:if(r[b].u>255||r[c].u>255||r[d].u>255)goto fault;r[a].u=r[b].u<<16|r[c].u<<8|r[d].u;break;
        case HSV:if(!isfinite(r[b].f)||!isfinite(r[c].f)||!isfinite(r[d].f)||r[c].f<0||r[c].f>1||r[d].f<0||r[d].f>1)goto fault;r[a].u=lbc_hsv(r[b].f,r[c].f,r[d].f);break;
        case IMIN:r[a].i=r[b].i<r[c].i?r[b].i:r[c].i;break;case IMAX:r[a].i=r[b].i>r[c].i?r[b].i:r[c].i;break;
        case FMIN:r[a].f=fminf(r[b].f,r[c].f);break;case FMAX:r[a].f=fmaxf(r[b].f,r[c].f);break;
        case HASH:r[a].f=lbc_hash01(r[b].i,r[c].i);break;
        case P_LOAD:if(!load_value(p,&p->resources[r[b].u],r[c].u,&r[a]))goto fault;break;
        case P_STORE:if(!store_value(&p->resources[r[a].u],r[b].u,r[c]))goto fault;break;
        case P_LEN:r[a].u=p->resources[r[b].u].count;break;
        case P_FILL:{
            PbcResource *resource=&p->resources[r[a].u];
            if(r[c].u>r[d].u||r[d].u>resource->count)goto fault;
            uint32_t start=r[c].u+p->bulk,end=start+32;if(end>r[d].u)end=r[d].u;
            for(unsigned k=start;k<end;k++)if(!store_value(resource,k,r[b]))goto fault;
            if(end<r[d].u){p->bulk=end-r[c].u;p->pc--;}else p->bulk=0;break;
        }
        case P_COPY:{
            PbcResource *dst=&p->resources[r[a].u],*src=&p->resources[r[b].u];
            if(dst->count!=src->count||dst->sealed)goto fault;
            if(dst==src)break;
            unsigned end=p->bulk+32;if(end>dst->count)end=dst->count;
            for(unsigned k=p->bulk;k<end;k++){PbcValue v;if(!load_value(p,src,k,&v)||!store_value(dst,k,v))goto fault;}
            if(end<dst->count){p->bulk=end;p->pc--;}else p->bulk=0;break;
        }
        case P_NATIVE:{PbcValue result={0};if(!native_call(p,&p->calls[d],&result))goto fault;if(a!=255)r[a]=result;break;}
        default:goto fault;
        }
    }
    return 0;
fault:p->status=UINT32_MAX;p->pending_cycles=0;p->completion_pending=false;return -1;
}
