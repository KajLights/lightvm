#include "portable.h"
#include "runtime.h"
#include "platform.h"
#include "vm_allocator.h"
#include "draw.h"
#include "portable_draw.h"
#include "portable_map.h"
#include <math.h>
#include <limits.h>
#include <string.h>

/* Opcodes 0..32 retain their numeric assignments, but v4 functions return to
 * their native caller; profile scheduling never lives in the interpreter. */
enum { P_REF=6,P_SETTING=6,P_LOAD=33,P_STORE,P_LEN,P_FILL,P_COPY,P_NATIVE,P_MAP };
enum { WRITE_STATE=1,WRITE_OUTPUT=4,USE_RNG=8 };
typedef struct { uint8_t result,argc,args[8],rights; } NativeSignature;
static const NativeSignature natives[]={
    {0,0,{0},0}, {0}, {0},
    {T_FLOAT,0,{0},USE_RNG}, {T_INT,3,{T_INT,T_INT,T_INT},USE_RNG},
    {0,1,{T_INT},USE_RNG}, {0,0,{0},USE_RNG},
    {T_FLOAT,1,{T_FLOAT},0}, {T_INT,1,{T_COLOR},0},
    {T_INT,1,{T_COLOR},0}, {T_INT,1,{T_COLOR},0},
    {T_COLOR,1,{T_FLOAT},0}, {T_COLOR,1,{T_INT},0},
    {T_FLOAT,1,{T_COLOR},0}, {0,1,{T_BOOL},0},
    {T_INT,1,{T_COLOR},0}, {T_COLOR,1,{T_INT},0}, {T_INT,2,{T_INT,T_INT},USE_RNG},
    /* Removed native IDs 18..26 are rejected during admission. */
    {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0},
    {T_BOOL,2,{P_REF,T_INT},0}, /* 27: channel query */
    {0,5,{P_REF,T_INT,T_INT,T_INT,T_INT},0},
    {0,4,{P_REF,T_FLOAT,T_INT,T_INT},0},
    {0,4,{P_REF,T_FLOAT,T_INT,T_INT},0},
    {0,4,{P_REF,P_REF,P_REF,T_INT},0},
    {0,4,{P_REF,T_FLOAT,T_FLOAT,T_FLOAT},WRITE_STATE},
    {0,6,{P_REF,T_FLOAT,T_FLOAT,T_COLOR,T_FLOAT,T_FLOAT},WRITE_STATE},
    {0,5,{P_REF,T_COLOR,T_INT,T_INT,T_INT},0},
    {0,2,{P_REF,P_REF},0},
    {0,4,{P_REF,P_REF,T_INT,T_INT},0},
    {T_INT,2,{P_REF,T_FLOAT},WRITE_STATE}, /* 37: reserve */
    {T_INT,2,{P_REF,T_INT},0},
    {0,2,{P_REF,T_INT},WRITE_STATE},
    {T_INT,3,{P_REF,T_INT,T_INT},0},
    {T_FLOAT,3,{P_REF,T_INT,T_INT},0},
    {0,4,{P_REF,T_INT,T_INT,T_INT},WRITE_STATE},
    {0,4,{P_REF,T_INT,T_INT,T_FLOAT},WRITE_STATE},
    {T_FLOAT,3,{P_REF,T_INT,T_FLOAT},0},
    {T_INT,1,{P_REF},0},
    {T_BOOL,2,{P_REF,T_INT},0},
    {T_FLOAT,3,{P_REF,T_INT,T_FLOAT},WRITE_STATE},
    {0,3,{P_REF,T_COLOR,T_COLOR},0},
    {0,3,{P_REF,T_FLOAT,T_FLOAT},0},
    {0,4,{P_REF,P_REF,T_FLOAT,T_FLOAT},0},
    {0,4,{P_REF,T_INT,T_COLOR,T_INT},0},
    {0,4,{P_REF,P_REF,P_REF,T_INT},0}, /* 52: periodic destination drawing */
};
unsigned pbc_type(const Portable *p,unsigned r){return r<p->nregs?p->descriptors[r]&15:0;}
unsigned pbc_role(const Portable *p,unsigned r){return r<p->nregs?p->descriptors[r]>>4:UINT_MAX;}
uint32_t pbc_initial(const Portable *p,unsigned r){
    uint8_t q[4];if(r>=p->nregs||!p->read(p->context,96+r*8+4,q,4)){((Portable*)p)->io_fault=true;return 0;}return lbc_u32(q);
}
PbcValue pbc_value(const Portable *p,unsigned r){
    PbcValue v={0};if(r>=p->nregs)return v;
    if(pbc_role(p,r)==R_CONST){v.u=pbc_initial(p,r);return v;}
    if(p->values)memcpy(&v,p->values+p->register_offsets[r],4);
    return v;
}
void pbc_set_value(Portable *p,unsigned r,PbcValue v){
    if(r<p->nregs&&p->values&&pbc_role(p,r)!=R_CONST)memcpy(p->values+p->register_offsets[r],&v,4);
}
static bool table_read(Portable *p,unsigned base,unsigned index,unsigned count,unsigned width,uint8_t *q){
    if(index>=count||!p->read(p->context,base+index*width,q,width)){memset(q,0,width);p->io_fault=true;return false;}return true;
}
PbcFunction pbc_function(Portable *p,unsigned n){uint8_t q[24];table_read(p,p->functions_offset,n,p->nfunctions,24,q);return (PbcFunction){lbc_u32(q),lbc_u32(q+4),lbc_u32(q+8),lbc_u32(q+12),lbc_u32(q+16),lbc_u32(q+20)};}
PbcResourceSpec pbc_spec(Portable *p,unsigned n){uint8_t q[24];table_read(p,p->specs_offset,n,p->nresources,24,q);return (PbcResourceSpec){lbc_u32(q),lbc_u32(q+4),lbc_u32(q+8),lbc_u32(q+12),lbc_u32(q+16),lbc_u32(q+20)};}
PbcExpression pbc_expression(Portable *p,unsigned n){uint8_t q[16];table_read(p,p->expressions_offset,n,p->nexpressions,16,q);return (PbcExpression){q[0],q[1],(uint16_t)(q[2]|q[3]<<8),lbc_u32(q+4),lbc_u32(q+8),lbc_u32(q+12)};}
PbcCall pbc_call(Portable *p,unsigned n){uint8_t q[16];table_read(p,p->calls_offset,n,p->ncalls,16,q);return (PbcCall){lbc_u32(q),lbc_u32(q+4),{q[8],q[9],q[10],q[11],q[12],q[13],q[14],q[15]}};}
PbcSetting pbc_setting(Portable *p,unsigned n){uint8_t q[16];table_read(p,p->settings_offset,n,p->nsettings,16,q);return (PbcSetting){lbc_u32(q),lbc_u32(q+4),lbc_u32(q+8),lbc_u32(q+12)};}
static bool finite_value(unsigned type,uint32_t value){
    return type==T_FLOAT ? (value&0x7f800000u)!=0x7f800000u :
        type==T_BOOL ? value<2 : type==T_COLOR ? value<=0xffffff : true;
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
bool pbc_read_instruction(Portable *p,unsigned pc,uint8_t out[8]){return pc<p->ncode&&instruction(p,pc,out);}
static unsigned element_type(unsigned kind){return kind==7||kind==8||kind==11?T_INT:draw_channels(kind)?T_COLOR:kind;}
static unsigned element_size(unsigned kind){
    if(kind>=DRAW_IMAGE_RGB&&kind<=DRAW_IMAGE_RGBWA)return draw_stride(kind);
    switch(kind){case 1:case 2:case 11:case DRAW_RGBA:return 4;case 7:return 1;case 8:return 2;default:return 0;}
}
static int resource_id(const Portable *p,unsigned reg){
    if(pbc_type(p,reg)!=P_REF || pbc_role(p,reg)!=R_CONST)return -1;
    uint32_t id=pbc_initial(p,reg);return id<p->nresources?(int)id:-1;
}
static bool can_write(const Portable *p,unsigned reg,unsigned rights){
    unsigned role=pbc_role(p,reg);
    return role==R_LOCAL || (role==R_STATE&&(rights&WRITE_STATE));
}
static bool resource_write(const Portable *p,unsigned reg,unsigned rights){
    int id=resource_id(p,reg);
    return id>=0 && !(pbc_spec((Portable*)p,id).flags&2u) && element_size(pbc_spec((Portable*)p,id).kind) && (rights&(pbc_spec((Portable*)p,id).flags&1u?WRITE_OUTPUT:WRITE_STATE));
}
static bool valid_instruction(Portable *p,unsigned pc,const PbcFunction *fn){
    uint8_t q[8];if(!instruction(p,pc,q))return false;
    unsigned op=q[0],a=q[1],b=q[2],c=q[3],at=pbc_type(p,a),bt=pbc_type(p,b),ct=pbc_type(p,c);
    uint32_t d=lbc_u32(q+4);
    if(pc+1==fn->end && op!=(fn->result?RETURN:END))return false;
    if(op==END)return !fn->result&&!a&&!b&&!c&&!d;
    if(op==RETURN)return fn->result && at==fn->result&&!b&&!c&&!d;
    if(op==JMP||op==JZ)return (op==JMP?!a:at==T_BOOL)&&!b&&!c&&d>=fn->first&&d<fn->end;
    if(op==P_STORE){int id=resource_id(p,a);return resource_write(p,a,fn->rights)&&bt==T_INT&&ct==element_type(pbc_spec((Portable*)p,id).kind)&&!d;}
    if(op==P_FILL){int id=resource_id(p,a);return resource_write(p,a,fn->rights)&&bt==element_type(pbc_spec((Portable*)p,id).kind)&&ct==T_INT&&pbc_type(p,d)==T_INT;}
    if(op==P_COPY){int x=resource_id(p,a),y=resource_id(p,b);return resource_write(p,a,fn->rights)&&y>=0&&(!(pbc_spec((Portable*)p,y).flags&1u)||(fn->rights&WRITE_OUTPUT))&&pbc_spec((Portable*)p,x).kind==pbc_spec((Portable*)p,y).kind&&!c&&!d;}
    if(op==P_MAP){
        int id=resource_id(p,a);
        return p->drawing_api>=4&&id>=0&&draw_channels(pbc_spec(p,(unsigned)id).kind)&&resource_write(p,a,fn->rights)&&
            bt==T_INT&&ct==T_INT&&pbc_role(p,b)==R_LOCAL&&d>pc+1&&d<fn->end&&
            pbc_map_valid(p,pc+1,d,a,b,c);
    }
    if(op==P_NATIVE){
        if(b||c||d>=p->ncalls)return false;
        PbcCall site_value=pbc_call(p,d);const PbcCall *site=&site_value;if((site->binding<3||(site->binding>=18&&site->binding<=26))||site->binding>=sizeof(natives)/sizeof(*natives))return false;
        const NativeSignature *sig=&natives[site->binding];
        if(site->argc!=sig->argc||(sig->rights&~fn->rights))return false;
        for(unsigned n=0;n<8;n++)if(n<site->argc?pbc_type(p,site->args[n])!=sig->args[n]:site->args[n]!=0)return false;
        if(site->binding>=27){if(!p->drawing_api||!pbc_drawing_valid(p,site,fn->rights))return false;}

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
    case P_LOAD:{int id=resource_id(p,b);return id>=0&&(!(pbc_spec((Portable*)p,id).flags&1u)||(fn->rights&WRITE_OUTPUT))&&at==element_type(pbc_spec((Portable*)p,id).kind)&&ct==T_INT&&!d;}
    case P_LEN:{int id=resource_id(p,b);return id>=0&&at==T_INT&&!c&&!d;}
    default:return false;
    }
}
static bool valid_expressions(Portable *p){
    uint32_t dependencies[128]={0};int setting_for_reg[128];
    for(unsigned r=0;r<128;r++)setting_for_reg[r]=-1;
    for(unsigned n=0;n<p->nsettings;n++){
        PbcSetting s_value=pbc_setting(p,n);const PbcSetting *s=&s_value;
        if(s->reg>=p->nregs||pbc_role(p,s->reg)!=P_SETTING||setting_for_reg[s->reg]>=0||s->initial>=p->nexpressions||
            (s->minimum!=UINT32_MAX&&s->minimum>=p->nexpressions)||(s->maximum!=UINT32_MAX&&s->maximum>=p->nexpressions))return false;
        setting_for_reg[s->reg]=(int)n;
    }
    for(unsigned r=0;r<p->nregs;r++)if(pbc_role(p,r)==P_SETTING&&setting_for_reg[r]<0)return false;
    for(unsigned n=0;n<p->nexpressions;n++){
        PbcExpression e_value=pbc_expression(p,n);const PbcExpression *e=&e_value;
        if(e->reserved||e->type<T_INT||e->type>T_BOOL)return false;
        if(e->op==0){if(e->a||e->b||!finite_value(e->type,e->value))return false;}
        else if(e->op==1){if(e->type!=T_INT||e->a||e->b||e->value)return false;}
        else if(e->op==2){
            if(e->a>=p->nregs||setting_for_reg[e->a]<0||e->type!=pbc_type(p,e->a)||e->b||e->value)return false;
            dependencies[n]=1u<<setting_for_reg[e->a];
        }else{
            if(e->op>10||e->a>=n||e->b>=n||e->value)return false;
            unsigned a=pbc_expression((Portable*)p,e->a).type,b=pbc_expression((Portable*)p,e->b).type;
            if((a!=T_INT&&a!=T_FLOAT)||(b!=T_INT&&b!=T_FLOAT))return false;
            unsigned expected=a==T_FLOAT||b==T_FLOAT||e->op==8?T_FLOAT:T_INT;
            if(e->type!=expected||(e->op==6&&expected!=T_INT))return false;
            dependencies[n]=dependencies[e->a]|dependencies[e->b];
        }
    }
    for(unsigned n=0;n<p->nsettings;n++){
        PbcSetting s_value=pbc_setting(p,n);const PbcSetting *s=&s_value;unsigned type=pbc_type(p,s->reg);uint32_t allowed=(1u<<n)-1;
        if(type!=pbc_expression((Portable*)p,s->initial).type||(dependencies[s->initial]&~allowed))return false;
        uint32_t bounds[2]={s->minimum,s->maximum};
        for(unsigned k=0;k<2;k++)if(bounds[k]!=UINT32_MAX){
            unsigned t=pbc_expression(p,bounds[k]).type;
            if((type!=T_INT&&type!=T_FLOAT)||(t!=type&&!(type==T_FLOAT&&t==T_INT))||(dependencies[bounds[k]]&~allowed))return false;
        }
    }
    runtime_checkpoint();return true;
}
void pbc_destroy(Portable *p){if(p){demo_free(p->state);demo_free(p->metadata);demo_free(p);}}
const char *pbc_open(Portable **out,LbcRead read,void *context,size_t size){
    *out=NULL;uint8_t h[96];
    if(size<96||size>32768||!read(context,0,h,96)||memcmp(h,"LBC1",4)||lbc_u32(h+4)!=4)return "invalid portable header";
    for(unsigned n=84;n<96;n+=4)if(lbc_u32(h+n))return "reserved portable header field";
    Portable *p=demo_calloc(1,sizeof(*p));if(!p)return "portable context exceeds memory budget";
    p->read=read;p->context=context;p->size=(uint32_t)size;p->drawing_api=lbc_u32(h+80);
    if(p->drawing_api&&p->drawing_api!=2&&p->drawing_api!=3&&p->drawing_api!=4){pbc_destroy(p);return "unsupported drawing API";}
    p->nregs=lbc_u32(h+8);p->ncode=lbc_u32(h+12);p->ncolors=lbc_u32(h+16);
    p->nfunctions=lbc_u32(h+20);p->nresources=lbc_u32(h+24);p->nexpressions=lbc_u32(h+28);
    p->ncalls=lbc_u32(h+32);p->nsettings=lbc_u32(h+36);
    for(unsigned n=0;n<2;n++)p->entries[n]=lbc_u32(h+40+4*n);
    for(unsigned n=48;n<72;n+=4)if(lbc_u32(h+n)){pbc_destroy(p);return "obsolete animation lifecycle; recompile source";}
    p->profile=lbc_u32(h+72);p->manifest_size=lbc_u32(h+76);
    const char *error="invalid portable limits or profile";
    if(!p->nregs||p->nregs>128||!p->ncode||p->ncode>2048||p->ncolors>1024||!p->nfunctions||p->nfunctions>16||
        p->nresources>16||!p->nexpressions||p->nexpressions>128||p->ncalls>256||p->nsettings>16||!p->manifest_size||p->manifest_size>8192)goto fail;
    p->code_offset=96+p->nregs*8;p->palette_offset=p->code_offset+p->ncode*8;
    p->functions_offset=p->palette_offset+p->ncolors*4;
    p->specs_offset=p->functions_offset+p->nfunctions*24;
    p->expressions_offset=p->specs_offset+p->nresources*24;
    p->calls_offset=p->expressions_offset+p->nexpressions*16;
    p->settings_offset=p->calls_offset+p->ncalls*16;
    p->manifest_offset=p->settings_offset+p->nsettings*16;
    if(p->manifest_offset+p->manifest_size!=size)goto fail;
    /* Only compact active type/layout bindings live in RAM. All immutable
       descriptors, templates and tables remain in the pinned package. */
    unsigned binding_bytes=(p->nregs+1u)&~1u;
    p->metadata_bytes=binding_bytes+p->nregs*sizeof(uint16_t);
    p->metadata=demo_calloc(1,p->metadata_bytes);if(!p->metadata){error="portable bindings exceed memory budget";goto fail;}
    p->descriptors=p->metadata;p->register_offsets=(uint16_t*)(p->metadata+binding_bytes);
    for(unsigned r=0;r<p->nregs;r++){
        uint8_t q[8];if(!read_at(p,96+r*8,q,8))goto fail;
        unsigned type=q[0],role=q[1];uint32_t value=lbc_u32(q+4);
        if(!type||type==5||type>P_REF||role==4||role==5||role>P_SETTING||q[2]||q[3]||!finite_value(type,value))goto fail;
        p->descriptors[r]=(uint8_t)(type|(role<<4));
        p->register_offsets[r]=(uint16_t)p->register_bytes;
        if(role!=R_CONST)p->register_bytes+=4;
        if(type==P_REF&&((role==R_CONST&&value>=p->nresources)||(role!=R_CONST&&role!=R_INPUT&&role!=R_LOCAL)||(role!=R_CONST&&value)))goto fail;
        if(role==P_SETTING&&(type>T_BOOL||value))goto fail;
    }
    for(unsigned n=0;n<p->ncalls;n++){
        PbcCall item=pbc_call(p,n);const PbcCall *call=&item;
        if((call->binding<3||(call->binding>=18&&call->binding<=26))||call->binding>=sizeof(natives)/sizeof(*natives)){error="invalid native binding";goto fail;}
        const NativeSignature *sig=&natives[call->binding];
        if(call->argc!=sig->argc)goto fail;
        for(unsigned k=0;k<8;k++)if(k<call->argc?pbc_type(p,call->args[k])!=sig->args[k]:call->args[k]!=0)goto fail;
    }
    error="invalid portable declarations";
    if(!valid_expressions(p))goto fail;
    for(unsigned n=0;n<p->nresources;n++){
        PbcResourceSpec s_value=pbc_spec(p,n);const PbcResourceSpec *s=&s_value;
        if(!s->kind||s->kind==3||s->kind==4||s->kind==5||s->kind==6||s->kind==9||s->kind==10||s->kind==13||s->kind>DRAW_STYLE||s->flags>2||(s->flags==2&&!(s->kind>=DRAW_IMAGE_RGB&&s->kind<=DRAW_IMAGE_RGBWA))||(!p->drawing_api&&s->kind>13)||s->length>=p->nexpressions||s->fill>=p->nexpressions||s->seed>=p->nexpressions||s->stream>=p->nexpressions)goto fail;
        if(pbc_expression((Portable*)p,s->length).type!=T_INT||pbc_expression((Portable*)p,s->seed).type!=T_INT||pbc_expression((Portable*)p,s->stream).type!=T_INT)goto fail;
        unsigned fill_type=pbc_expression((Portable*)p,s->fill).type;
        if(fill_type!=(s->kind==DRAW_BATCH||s->kind==DRAW_STYLE?T_INT:element_type(s->kind)))goto fail;
    }
    unsigned previous=0;
    for(unsigned n=0;n<p->nfunctions;n++){
        PbcFunction fn_value=pbc_function(p,n);const PbcFunction *fn=&fn_value;
        if(fn->first!=previous||fn->end<=fn->first||fn->end>p->ncode||(fn->rights&~13u)||(fn->result==5||fn->result>P_REF)||fn->arg_count>8||fn->arg_start>p->nregs||fn->arg_count>p->nregs-fn->arg_start)goto fail;
        for(unsigned k=0;k<fn->arg_count;k++)if(pbc_role(p,fn->arg_start+k)!=R_INPUT)goto fail;
        previous=fn->end;
        for(unsigned pc=fn->first;pc<fn->end;pc++){if(!valid_instruction(p,pc,fn)){error="invalid portable instruction or effect permission";goto fail;}if((pc&31u)==31u)runtime_checkpoint();}
    }
    if(previous!=p->ncode)goto fail;
    for(unsigned n=0;n<p->ncolors;n++){uint8_t q[4];if(!read_at(p,p->palette_offset+4*n,q,4)||lbc_u32(q)>0xffffff)goto fail;}
    if(p->io_fault)goto fail;
    p->status=1;*out=p;return NULL;
fail:pbc_destroy(p);return error;
}
static double number(PbcValue value,unsigned type){return type==T_FLOAT?value.f:value.i;}
static bool evaluate(Portable *p,unsigned id,unsigned count,const PbcValue *bindings,PbcValue *out){
    PbcValue values[128]={0};
    for(unsigned n=0;n<=id;n++){
        PbcExpression e_value=pbc_expression(p,n);const PbcExpression *e=&e_value;PbcValue v={0};
        if(e->op==0)v.u=e->value;
        else if(e->op==1)v.i=(int32_t)count;
        else if(e->op==2)v=bindings?bindings[e->a]:pbc_value(p,e->a);
        else if(e->type==T_INT){
            int64_t a=values[e->a].i,b=values[e->b].i,result;
            switch(e->op){
            case 3:result=a+b;break;case 4:result=a-b;break;case 5:result=a*b;break;
            case 6:case 7:if(!b)return false;result=a/b;if(a%b&&((a%b<0)!=(b<0)))result--;if(e->op==7)result=a-result*b;break;
            case 9:result=a<b?a:b;break;case 10:result=a>b?a:b;break;default:return false;}
            if(result<INT32_MIN||result>INT32_MAX)return false;
            v.i=(int32_t)result;
        }else{
            float a=(float)number(values[e->a],pbc_expression((Portable*)p,e->a).type),b=(float)number(values[e->b],pbc_expression((Portable*)p,e->b).type);
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
    if(r->kind>=DRAW_IMAGE_RGB&&r->kind<=DRAW_IMAGE_RGBWA){
        if(value.u>0xffffff)return false;
        memset(at,0,draw_stride(r->kind));at[0]=(uint8_t)(value.u>>16);at[1]=(uint8_t)(value.u>>8);at[2]=(uint8_t)value.u;
        int alpha=draw_channel(r->kind,4);if(alpha>=0)at[alpha]=255;
        return true;
    }
    switch(r->kind){
    case 11:if(value.i<0)return false;/* Supported integer domain is signed i32. */
        /* fall through */
    case 1:memcpy(at,&value.i,4);break;
    case 2:if(!isfinite(value.f))return false;memcpy(at,&value.f,4);break;
    case DRAW_RGBA:if(value.u>0xffffff)return false;draw_rgba(at,value.u,255);break;
    case 7:if(value.u>255)return false;*at=(uint8_t)value.u;break;
    case 8:if(value.u>65535)return false;{uint16_t v=(uint16_t)value.u;memcpy(at,&v,2);}break;
    default:return false;
    }
    return true;
}
static bool load_value(Portable *p,PbcResource *r,unsigned index,PbcValue *value){
    if(index>=r->count)return false;
    memset(value,0,sizeof(*value));
    if(r->kind>=DRAW_IMAGE_RGB&&r->kind<=DRAW_IMAGE_RGBWA){uint8_t v[5];if(!pbc_image_read(p,r,index,v))return false;value->u=(uint32_t)v[0]<<16|(uint32_t)v[1]<<8|v[2];return true;}
    if(!element_size(r->kind)||!r->data)return false;
    const uint8_t *at=r->data+index*element_size(r->kind);
    switch(r->kind){
    case 1:case 11:memcpy(&value->i,at,4);break;case 2:memcpy(&value->f,at,4);break;
    case DRAW_RGBA:value->u=0;for(unsigned c=0;c<3;c++)value->u=(value->u<<8)|(at[3]?((unsigned)at[c]*255u+at[3]/2u)/at[3]:0);break;
    case 7:value->u=*at;break;
    case 8:{uint16_t v;memcpy(&v,at,2);value->u=v;}break;default:return false;
    }return true;
}
static bool resolve_settings(Portable *p,unsigned count,PbcValue *bindings){
    for(unsigned n=0;n<p->nsettings;n++){
        strcpy(p->argument_error,p->setting_names[n]);
        PbcSetting s_value=pbc_setting(p,n);const PbcSetting *s=&s_value;PbcValue value,lo,hi;
        if(!evaluate(p,s->initial,count,bindings,&value))return false;
        if(p->override_mask&(1u<<n))value=p->overrides[n];
        unsigned typ=pbc_type(p,s->reg);double v=number(value,typ);
        if(s->minimum!=UINT32_MAX&&(!evaluate(p,s->minimum,count,bindings,&lo)||v<number(lo,pbc_expression((Portable*)p,s->minimum).type)))return false;
        if(s->maximum!=UINT32_MAX&&(!evaluate(p,s->maximum,count,bindings,&hi)||v>number(hi,pbc_expression((Portable*)p,s->maximum).type)))return false;
        if(bindings)bindings[s->reg]=value;else pbc_set_value(p,s->reg,value);
    }
    p->argument_error[0]=0;
    return true;
}
static void random_default(Portable *p);
const char *pbc_prepare(Portable *p,LbcBuffer *output){
    if(p->initialized||p->state||(output&&(!output->data||!output->count||output->count>65535||output->sealed)))return "invalid resource preparation";
    unsigned count=output?output->count:0;
    size_t resource_offset=(p->register_bytes+_Alignof(PbcResource)-1)&~(size_t)(_Alignof(PbcResource)-1);
    p->state_bytes=resource_offset+p->nresources*sizeof(PbcResource);
    if(p->drawing_api)p->state_bytes=(p->state_bytes+7u)&~(size_t)7u;
    p->state=demo_calloc(1,p->state_bytes);if(!p->state)return "portable state exceeds memory budget";
    p->values=p->state;p->resources=(PbcResource*)(p->values+resource_offset);p->generation=output?output->generation:0;p->output=output;
    for(unsigned r=0;r<p->nregs;r++)pbc_set_value(p,r,(PbcValue){.u=pbc_initial(p,r)});
    const char *error="invalid setting or resource expression";
    if(!resolve_settings(p,count,NULL))goto fail;
    size_t payload=0;
    for(unsigned n=0;n<p->nresources;n++){
        PbcResourceSpec s_value=pbc_spec(p,n);const PbcResourceSpec *s=&s_value;PbcResource *r=&p->resources[n];PbcValue length,fill,seed,stream;
        if(!evaluate(p,s->length,count,NULL,&length)||length.i<0||length.i>65535||!evaluate(p,s->fill,count,NULL,&fill))goto fail;
        r->kind=s->kind;r->count=(uint32_t)length.i;r->generation=p->generation;
        if(s->flags&1u){
            if(!output||s->kind!=DRAW_RGBA||r->count!=output->count)goto fail;
            r->data=output->data;continue;
        }
        if(s->kind==DRAW_BATCH||s->kind==DRAW_STYLE){
            if(!r->count||r->count>65535||!evaluate(p,s->seed,count,NULL,&seed)||!evaluate(p,s->stream,count,NULL,&stream))goto fail;
            if(s->kind==DRAW_BATCH&&(seed.u<8||seed.u>24||pbc_expression(p,s->stream).op!=0||(stream.u>>seed.u)))goto fail;
            if(s->kind==DRAW_STYLE&&r->count!=1)goto fail;
            r->random_state=seed.u;payload=(payload+7u)&~(size_t)7u;
            size_t bytes=s->kind==DRAW_STYLE?sizeof(DrawStyle):draw_batch_bytes(r->count,seed.u);
            if(payload>VM_HEAP_LIMIT||bytes>VM_HEAP_LIMIT-payload)goto fail;
            payload+=bytes;
        }else if(s->kind>=DRAW_IMAGE_RGB&&s->kind<=DRAW_IMAGE_RGBWA){
            if(!evaluate(p,s->seed,count,NULL,&seed)||!evaluate(p,s->stream,count,NULL,&stream)||seed.u>p->ncolors)goto fail;
            size_t bytes=(size_t)r->count*draw_stride(s->kind);
            if(stream.u&&((size_t)stream.u!=bytes||(bytes+2)/3>p->ncolors-seed.u))goto fail;
            r->random_state=seed.u;r->sealed=s->flags==2;
            if(r->sealed&&!stream.u)goto fail;
            if(!r->sealed){if(payload>VM_HEAP_LIMIT||bytes>VM_HEAP_LIMIT-payload)goto fail;payload+=bytes;}
        }else{
            size_t bytes=r->count*element_size(r->kind);
            if(bytes>VM_HEAP_LIMIT-payload)goto fail;
            payload+=bytes;
        }
    }
    /* Reallocate once at admission to include payload; no execution-time heap. */
    if(payload){
        void *larger=demo_calloc(1,p->state_bytes+payload);if(!larger){error="portable resources exceed peak memory budget";goto fail;}
        memcpy(larger,p->state,p->state_bytes);demo_free(p->state);p->state=larger;
        p->values=p->state;p->resources=(PbcResource*)(p->values+resource_offset);
    }
    p->payload_bytes=payload;uint8_t *cursor=(uint8_t*)p->state+p->state_bytes;p->state_bytes+=payload;
    for(unsigned n=0;n<p->nresources;n++){
        PbcResource *r=&p->resources[n];if(pbc_spec((Portable*)p,n).flags)continue;
        if(r->kind==DRAW_BATCH||r->kind==DRAW_STYLE){cursor=(uint8_t*)(((uintptr_t)cursor+7u)&~(uintptr_t)7u);r->data=cursor;cursor+=r->kind==DRAW_STYLE?sizeof(DrawStyle):draw_batch_bytes(r->count,r->random_state);continue;}
        if(r->kind>=DRAW_IMAGE_RGB&&r->kind<=DRAW_IMAGE_RGBWA){
            r->data=cursor;unsigned stride=draw_stride(r->kind);cursor+=(size_t)r->count*stride;PbcValue bytes;
            if(!evaluate(p,pbc_spec(p,n).stream,count,NULL,&bytes))goto fail;
            if(bytes.u){PbcResource source=*r;source.data=NULL;for(unsigned k=0;k<r->count;k++){uint8_t v[5];if(!pbc_image_read(p,&source,k,v))goto fail;for(unsigned c=0;c<5;c++){int at=draw_channel(r->kind,c);if(at>=0)r->data[(size_t)k*stride+at]=v[c];}if((k&31u)==31u)runtime_checkpoint();}}
            continue;
        }
        if(!element_size(r->kind))continue;
        r->data=cursor;cursor+=r->count*element_size(r->kind);PbcValue fill;
        if(!evaluate(p,pbc_spec((Portable*)p,n).fill,count,NULL,&fill))goto fail;
        for(unsigned k=0;k<r->count;k++){if(!store_value(r,k,fill))goto fail;if((k&31u)==31u)runtime_checkpoint();}
    }
    random_default(p);p->initialized=true;return NULL;
fail:demo_free(p->state);p->state=NULL;p->values=NULL;p->resources=NULL;p->state_bytes=0;return error;
}
bool pbc_invoke(Portable *p,unsigned function,const PbcValue *arguments,unsigned count){
    if(!p->initialized||p->status==0||function>=p->nfunctions)return false;
    PbcFunction fn_value=pbc_function(p,function);const PbcFunction *fn=&fn_value;
    if(count!=fn->arg_count||(count&&!arguments))return false;
    for(unsigned n=0;n<count;n++){
        unsigned type=pbc_type(p,fn->arg_start+n);
        if(!finite_value(type,arguments[n].u)||(type==P_REF&&arguments[n].u>=p->nresources))return false;
    }
    for(unsigned r=0;r<p->nregs;r++)if(pbc_role(p,r)==R_LOCAL){
        pbc_set_value(p,r,(PbcValue){.u=pbc_initial(p,r)});
    }
    for(unsigned n=0;n<count;n++)pbc_set_value(p,fn->arg_start+n,arguments[n]);
    p->function=function;p->pc=fn->first;p->used=0;p->bulk=0;p->status=0;return true;
}
/* PCG32 state belongs to the portable instance. Rejection consumes one VM
 * dispatch, so standard integer sampling can yield without reporting failure. */
static uint32_t random_word(Portable *p){
    uint64_t old=p->random_state;
    p->random_state=old*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407);
    uint32_t x=(uint32_t)(((old>>18)^old)>>27),rotation=(uint32_t)(old>>59);
    return (x>>rotation)|(x<<((-rotation)&31));
}
static void random_seed(Portable *p,uint64_t seed){
    p->random_state=0;random_word(p);p->random_state+=seed;random_word(p);
}
static void random_default(Portable *p){
    static uint64_t sequence;
    random_seed(p,platform_time_us()+(++sequence)*UINT64_C(0x9e3779b97f4a7c15));
}
static bool random_range(Portable *p,int64_t start,int64_t stop,int64_t step,PbcValue *result){
    if(!step||(step>0?start>=stop:start<=stop))return false;
    uint64_t count=(uint64_t)(step>0?(stop-start-1)/step+1:(start-stop-1)/(-step)+1);
    if(!count||count>UINT64_C(4294967296))return false;
    uint64_t limit=UINT64_C(4294967296)-(UINT64_C(4294967296)%count);
    uint32_t word=random_word(p);
    if(word>=limit){p->native_pending=true;return true;}
    int64_t value=start+(int64_t)(word%count)*step;
    if(value<INT32_MIN||value>INT32_MAX)return false;
    result->i=(int32_t)value;return true;
}
/* Native signatures and permission checks above are shared for every binding.
 * Bodies below implement library versions, not compiler-specific AST behaviour. */
static bool native_call(Portable *p,const PbcCall *site,PbcValue *result){
    PbcValue a[8]={{0}};for(unsigned k=0;k<site->argc;k++)a[k]=pbc_value(p,site->args[k]);
    if(site->binding>=18){uint64_t started=platform_time_us();bool ok=pbc_drawing_call(p,site,a,result);p->native_draw_us+=platform_time_us()-started;return ok;}
    switch(site->binding){
    case 3:result->f=(random_word(p)>>8)*0x1p-24f;return true;
    case 4:return random_range(p,a[0].i,a[1].i,a[2].i,result);
    case 5:random_seed(p,a[0].i<0?(uint64_t)-(int64_t)a[0].i:(uint64_t)a[0].i);return true;
    case 6:random_default(p);return true;
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
    case 15:result->u=a[0].u;return true;
    case 16:if(a[0].u>0xffffff)return false;result->u=a[0].u;return true;
    case 17:return random_range(p,a[0].i,(int64_t)a[1].i+1,1,result);
    default:return false;
    }
}
int pbc_resume(Portable *p,unsigned quota){
    if(p->status)return p->status==1?1:-1;
    p->slice_count++;
    for(unsigned n=0;n<p->nresources;n++)if(p->resources[n].generation!=p->generation){p->status=UINT32_MAX;return -1;}
    if(p->output&&(p->output->sealed||p->output->generation!=p->generation)){p->status=UINT32_MAX;return -1;}

    while(quota--){
        uint8_t q[8];if(!instruction(p,p->pc,q))goto fault;
        unsigned op=q[0],a=q[1],b=q[2],c=q[3];uint32_t d=lbc_u32(q+4);p->pc++;p->used++;p->dispatch_count++;
        PbcValue r[4]={pbc_value(p,a),pbc_value(p,b),pbc_value(p,c),pbc_value(p,d)};
        if(p->io_fault)goto fault;
        bool writes=true;
        switch(op){
        case END:p->status=1;return 1;
        case RETURN:p->result=r[0];p->status=1;return 1;
        case MOV:r[0]=r[1];break;
        case IADD:r[0].u=r[1].u+r[2].u;break;case ISUB:r[0].u=r[1].u-r[2].u;break;case IMUL:r[0].u=r[1].u*r[2].u;break;
        case IDIV:case IMOD:{int32_t x=r[1].i,y=r[2].i;if(!y||(x==INT32_MIN&&y==-1))goto fault;int32_t div=x/y,rem=x%y;if(rem&&((rem<0)!=(y<0))){div--;rem+=y;}r[0].i=op==IDIV?div:rem;break;}
        case FADD:r[0].f=r[1].f+r[2].f;break;case FSUB:r[0].f=r[1].f-r[2].f;break;case FMUL:r[0].f=r[1].f*r[2].f;break;
        case FDIV:if(r[2].f==0)goto fault;r[0].f=r[1].f/r[2].f;break;
        case FMOD:if(!isfinite(r[1].f)||!isfinite(r[2].f)||r[2].f==0)goto fault;r[0].f=r[1].f-floorf(r[1].f/r[2].f)*r[2].f;break;
        case ITOF:r[0].f=(float)r[1].i;break;
        case FTOI:if(!isfinite(r[1].f)||r[1].f<-2147483648.0f||r[1].f>=2147483648.0f)goto fault;r[0].i=(int32_t)r[1].f;break;
        case ILT:r[0].u=r[1].i<r[2].i;break;case FLT:r[0].u=r[1].f<r[2].f;break;
        case EQ:r[0].u=pbc_type(p,b)==T_FLOAT?r[1].f==r[2].f:r[1].u==r[2].u;break;
        case JZ:writes=false;if(!r[0].u)p->pc=d;break;case JMP:writes=false;p->pc=d;break;
        case RGB:if(r[1].u>255||r[2].u>255||r[3].u>255)goto fault;r[0].u=r[1].u<<16|r[2].u<<8|r[3].u;break;
        case HSV:if(!isfinite(r[1].f)||!isfinite(r[2].f)||!isfinite(r[3].f)||r[2].f<0||r[2].f>1||r[3].f<0||r[3].f>1)goto fault;r[0].u=lbc_hsv(r[1].f,r[2].f,r[3].f);break;
        case IMIN:r[0].i=r[1].i<r[2].i?r[1].i:r[2].i;break;case IMAX:r[0].i=r[1].i>r[2].i?r[1].i:r[2].i;break;
        case FMIN:r[0].f=fminf(r[1].f,r[2].f);break;case FMAX:r[0].f=fmaxf(r[1].f,r[2].f);break;
        case HASH:r[0].f=lbc_hash01(r[1].i,r[2].i);break;
        case P_LOAD:if(!load_value(p,&p->resources[r[1].u],r[2].u,&r[0]))goto fault;break;
        case P_STORE:writes=false;if(!store_value(&p->resources[r[0].u],r[1].u,r[2]))goto fault;break;
        case P_LEN:r[0].u=p->resources[r[1].u].count;break;
        case P_FILL:writes=false;{
            PbcResource *resource=&p->resources[r[0].u];
            if(r[2].u>r[3].u||r[3].u>resource->count)goto fault;
            uint32_t start=r[2].u+p->bulk,end=start+32;if(end>r[3].u)end=r[3].u;
            for(unsigned k=start;k<end;k++)if(!store_value(resource,k,r[1]))goto fault;
            if(end<r[3].u){p->bulk=end-r[2].u;p->pc--;}else p->bulk=0;break;
        }
        case P_COPY:writes=false;{
            PbcResource *dst=&p->resources[r[0].u],*src=&p->resources[r[1].u];
            if(dst->count!=src->count||dst->sealed)goto fault;
            if(dst==src)break;
            unsigned end=p->bulk+32;if(end>dst->count)end=dst->count;
            for(unsigned k=p->bulk;k<end;k++){PbcValue v;if(!load_value(p,src,k,&v)||!store_value(dst,k,v))goto fault;}
            if(end<dst->count){p->bulk=end;p->pc--;}else p->bulk=0;break;
        }
        case P_NATIVE:{PbcValue result={0};PbcCall site=pbc_call(p,d);p->native_pending=false;if(!native_call(p,&site,&result))goto fault;if(p->native_pending){p->pc--;writes=false;}else if(a!=255)r[0]=result;else writes=false;break;}
        case P_MAP:{
            writes=false;p->native_pending=false;uint64_t started=platform_time_us();
            bool ok=pbc_map_run(p,p->pc,d,a,b,c);p->native_draw_us+=platform_time_us()-started;
            if(!ok)goto fault;
            if(p->native_pending)p->pc--;else p->pc=d;
            break;
        }
        default:goto fault;
        }
        if(writes)pbc_set_value(p,a,r[0]);
    }
    return 0;
fault:p->status=UINT32_MAX;return -1;
}
