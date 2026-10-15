#include "bytecode.h"
#include <math.h>
#include <string.h>
#include <limits.h>
#include <float.h>
_Static_assert(sizeof(float)==4 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,"LBC1 requires IEEE binary32");
static inline bool finite32(float f) { LbcValue v={.f=f};return (v.u&0x7f800000u)!=0x7f800000u; }
uint32_t lbc_u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static inline uint32_t aligned_u32(const uint8_t *p) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t v;memcpy(&v,__builtin_assume_aligned(p,4),4);return v;
#else
    return lbc_u32(p);
#endif
}
static unsigned type(const LbcProgram *p,unsigned r) { return r<p->nregs?p->registers[r*8]:0; }
static bool writable(const LbcProgram *p,unsigned r,unsigned stage) {
    if(r>=p->nregs)return false;
    unsigned role=p->registers[r*8+1];
    return role==R_LOCAL || (role==R_STATE && stage==0) || (role==R_FRAME && stage==1);
}
static const uint8_t *fetch(LbcProgram *p,uint32_t at,size_t size) {
    if(at>p->size || size>p->size-at)return NULL;
    if(p->raw)return p->raw+at;
    if(at<p->cache_offset || at+size>p->cache_offset+p->cache_size) {
        p->cache_offset=at&~255u;
        p->cache_size=p->size-p->cache_offset;
        if(p->cache_size>sizeof(p->cache))p->cache_size=sizeof(p->cache);
        if(!p->read(p->context,p->cache_offset,p->cache,p->cache_size)){p->cache_size=0;return NULL;}
    }
    return p->cache+(at-p->cache_offset);
}
static bool open_program(LbcProgram *p,const uint8_t *r,size_t size) {
    if(size<64 || size>LBC_MAX_BYTES || memcmp(r,"LBC1",4) ||
       (lbc_u32(r+4)!=1 && lbc_u32(r+4)!=2) || (lbc_u32(r+32)!=1&&lbc_u32(r+32)!=2))return false;
    uint32_t manifest=lbc_u32(r+36);
    if(lbc_u32(r+4)==1 && manifest)return false;
    for(unsigned k=40;k<64;k+=4)if(lbc_u32(r+k))return false;
    uint32_t regs=lbc_u32(r+8),code=lbc_u32(r+12),colors=lbc_u32(r+16);
    uint32_t end=64+regs*8+code*8+colors*4;
    if(regs<4 || regs>LBC_REGS || code<3 || code>LBC_CODE || colors>1024 ||
       manifest>8192 || size!=end+manifest)return false;
    p->size=(uint32_t)size;p->nregs=regs;p->ncode=code;p->ncolors=colors;p->mode=lbc_u32(r+32);
    p->code_offset=64+regs*8;p->palette_offset=p->code_offset+code*8;
    p->manifest_offset=end;p->manifest_size=manifest;
    for(unsigned k=0;k<3;k++)p->entry[k]=lbc_u32(r+20+4*k);
    if(p->entry[0]!=0 || p->entry[1]<=p->entry[0] || p->entry[2]<=p->entry[1] || p->entry[2]>=code)return false;
    if(p->raw) {
        p->registers=p->raw+64;p->code=p->raw+p->code_offset;p->palette=p->raw+p->palette_offset;
    } else {
        if(!p->read(p->context,64,p->descriptors,regs*8))return false;
        p->registers=p->descriptors;
    }
    for(unsigned k=0;k<regs;k++) {
        const uint8_t *q=p->registers+k*8;LbcValue v={.u=lbc_u32(q+4)};
        if(q[0]<T_INT || q[0]>T_BOOL || q[1]>R_PARAM || q[2] || q[3])return false;
        if(k<4 && (q[1]!=R_INPUT || q[0]!=(k<2?T_INT:T_FLOAT) || v.u))return false;
        if(k>=4 && q[1]==R_INPUT)return false;
        if((q[0]==T_FLOAT && !finite32(v.f)) || (q[0]==T_BOOL && v.u>1) || (q[0]==T_COLOR && v.u>0xffffff))return false;
    }
    for(unsigned k=0;k<colors;k++) {
        const uint8_t *q=fetch(p,p->palette_offset+4*k,4);
        if(!q || lbc_u32(q)>0xffffff)return false;
    }
    return true;
}
bool lbc_open(LbcProgram *p,const void *bytes,size_t size) {
    memset(p,0,sizeof(*p));p->raw=bytes;
    return bytes && !((uintptr_t)bytes&3) && open_program(p,bytes,size);
}
bool lbc_open_reader(LbcProgram *p,LbcRead read,void *context,size_t size) {
    uint8_t header[64];memset(p,0,sizeof(*p));p->read=read;p->context=context;
    return size>=64 && read(context,0,header,sizeof(header)) && open_program(p,header,size);
}
static bool valid(LbcProgram *p,unsigned pc) {
    const uint8_t *q=fetch(p,p->code_offset+pc*8,8);if(!q)return false;unsigned op=q[0],a=q[1],b=q[2],c=q[3];uint32_t d=lbc_u32(q+4);
    unsigned stage=pc<p->entry[1]?0:pc<p->entry[2]?1:2,limit=stage<2?p->entry[stage+1]:p->ncode;
    unsigned at=type(p,a),bt=type(p,b),ct=type(p,c);
    /* Falling through into a different entry point is never permitted. */
    if(pc+1==limit && op!=(stage==2&&p->mode==1?RETURN:END) && !(stage==2 && (op==HSV_RETURN||op==HUE_RETURN)))return false;
    if(op==END)return (stage<2||p->mode==2) && !a&&!b&&!c&&!d;
    if(op==RETURN)return stage==2 && p->mode==1 && at==T_COLOR&&!b&&!c&&!d;
    if(op==HUE_RETURN)return stage==2&&p->mode==1&&at==T_FLOAT&&!b&&!c&&!d;
    if(op==HSV_RETURN)return stage==2 && p->mode==1 && at==T_FLOAT&&bt==T_FLOAT&&!c&&type(p,d)==T_FLOAT;
    if(op==JMP || op==JZ)return (op==JMP?!a:at==T_BOOL)&&!b&&!c&&d>=p->entry[stage]&&d<limit;
    if(op==STORE)return stage==2&&p->mode==2&&at==T_INT&&bt==T_COLOR&&!c&&!d;
    if(op==FILL)return stage==2&&p->mode==2&&at==T_COLOR&&!b&&!c&&!d;
    if(!writable(p,a,stage))return false;
    switch(op) {
    case MOV:return at==bt&&!c&&!d;
    case IADD:case ISUB:case IMUL:case IDIV:case IMOD:case IMIN:case IMAX:return at==T_INT&&bt==T_INT&&ct==T_INT&&!d;
    case FADD:case FSUB:case FMUL:case FDIV:case FMOD:case FMIN:case FMAX:return at==T_FLOAT&&bt==T_FLOAT&&ct==T_FLOAT&&!d;
    case ITOF:return at==T_FLOAT&&bt==T_INT&&!c&&!d;
    case FTOI:return at==T_INT&&bt==T_FLOAT&&!c&&!d;
    case ILT:return at==T_BOOL&&bt==T_INT&&ct==T_INT&&!d;
    case FLT:return at==T_BOOL&&bt==T_FLOAT&&ct==T_FLOAT&&!d;
    case EQ:return at==T_BOOL&&bt&&bt==ct&&!d;
    case RGB:return at==T_COLOR&&bt==T_INT&&ct==T_INT&&type(p,d)==T_INT;
    case HSV:return at==T_COLOR&&bt==T_FLOAT&&ct==T_FLOAT&&type(p,d)==T_FLOAT;
    case PALETTE:return at==T_COLOR&&bt==T_INT&&c&&d<=p->ncolors&&c<=p->ncolors-d;
    case IFMADD:return at==T_FLOAT&&bt==T_INT&&ct==T_FLOAT&&type(p,d)==T_FLOAT;
    case HASH:return at==T_FLOAT&&bt==T_INT&&ct==T_INT&&!d;
    default:return false;
    }
}
int lbc_validate(LbcProgram *p,unsigned quota) {
    while(quota-- && p->validated<p->ncode) {
        if(!valid(p,p->validated))return -1;
        p->validated++;
    }
    return p->validated==p->ncode;
}
bool lbc_init(LbcInstance *i,LbcProgram *p,LbcBuffer *buffer) {
    if(p->validated!=p->ncode || !buffer || !buffer->data || !buffer->count || buffer->count>65535 || buffer->sealed)return false;
    memset(i,0,sizeof(*i));i->program=p;i->buffer=buffer;i->generation=buffer->generation;i->status=1;
    for(unsigned k=0;k<p->nregs;k++)i->r[k].u=lbc_u32(p->registers+8*k+4);
    i->r[0].i=(int32_t)buffer->count;return true;
}
bool lbc_begin(LbcInstance *i,float time,float delta) {
    if(i->status==0 || !finite32(time) || !finite32(delta) || delta<0 || i->buffer->sealed || i->buffer->generation!=i->generation)return false;
    i->r[1].i=0;i->r[2].f=time;i->r[3].f=delta;i->pc=i->program->entry[0];i->stage=0;i->used=0;i->bulk=0;i->tick_only=false;i->status=0;return true;
}
bool lbc_tick(LbcInstance *i,float time,float delta) {
    if(!lbc_begin(i,time,delta))return false;
    i->tick_only=true;return true;
}
bool lbc_render(LbcInstance *i,float time) {
    if(!lbc_begin(i,time,0))return false;
    i->stage=1;i->pc=i->program->entry[1];return true;
}

bool lbc_parameter(LbcInstance *i,unsigned reg,uint32_t bits) {
    if(i->status==0 || reg>=i->program->nregs || i->program->registers[8*reg+1]!=R_PARAM)return false;
    LbcValue v={.u=bits};unsigned t=type(i->program,reg);
    if((t==T_FLOAT&&!finite32(v.f)) || (t==T_BOOL&&bits>1) || (t==T_COLOR&&bits>0xffffff))return false;
    i->r[reg]=v;return true;
}
/* Same versioned conversion is used by bytecode and the native reference.
 * Float arithmetic is deliberately retained: this is not an integer-only rainbow. */
static inline uint32_t quantized_hsv(uint32_t hue,uint32_t sat,uint32_t value) {
    uint32_t sector=(hue*6)>>16,fraction=(hue*6)&65535,denominator=255*65535;
    uint32_t low=(value*(255-sat)+127)/255;
    uint32_t q=value-(value*sat*fraction+denominator/2)/denominator;
    uint32_t t=value-(value*sat*(65535-fraction)+denominator/2)/denominator;
    switch(sector) {
    case 0:return value<<16|t<<8|low;case 1:return q<<16|value<<8|low;
    case 2:return low<<16|value<<8|t;case 3:return low<<16|q<<8|value;
    case 4:return t<<16|low<<8|value;default:return value<<16|low<<8|q;
    }
}
static inline uint32_t full_hue(float h) {
    uint32_t hue=(uint32_t)((h-floorf(h))*65536.0f+0.5f)&65535;
    return quantized_hsv(hue,255,255);
}
uint32_t lbc_hsv(float h,float s,float v) {
    if(!finite32(h)||!finite32(s)||!finite32(v)||s<0||s>1||v<0||v>1)return 0;
    uint32_t hue=(uint32_t)((h-floorf(h))*65536.0f+0.5f)&65535;
    return quantized_hsv(hue,(uint32_t)(s*255.0f+0.5f),(uint32_t)(v*255.0f+0.5f));
}
float lbc_hash01(int32_t index,int32_t seed) {
    /* Matches the original sample's polynomial hash, with explicit i32 inputs. */
    int64_t x=((int64_t)index+(int64_t)seed*131)%65521;if(x<0)x+=65521;
    x=(x*x*17+x*31+101)%65521;
    return (float)x/65521.0f;
}
static inline void write_rgb(uint8_t *p,uint32_t rgb) { p[0]=(uint8_t)(rgb>>16);p[1]=(uint8_t)(rgb>>8);p[2]=(uint8_t)rgb; }
void lbc_native_rainbow(uint8_t *rgb,unsigned start,unsigned end,float phase,float stride) {
    for(unsigned k=start;k<end;k++){float hue=phase+(float)k*stride;write_rgb(rgb+3*k,finite32(hue)?full_hue(hue):0);}
}
int lbc_resume(LbcInstance *i,unsigned quota) {
    if(i->status)return i->status;
    if(i->buffer->sealed || i->buffer->generation!=i->generation)return i->status=-1;
    LbcProgram *p=i->program;LbcValue *r=i->r;uint32_t pc=i->pc;unsigned used=0;
    /* Static instruction/type checks happened at admission. Ownership is stable
     * during this single-owner slice. Only dynamic value checks remain here. */
    if(i->used>=2000000)return i->status=-1;
    if(quota>2000000-i->used)quota=2000000-i->used;
    while(used<quota) {
        const uint8_t *q=fetch(p,p->code_offset+pc*8,8);if(!q)goto fault;uint32_t word=aligned_u32(q);
        unsigned op=word&255,a=(word>>8)&255,b=(word>>16)&255,c=word>>24;
        uint32_t d=aligned_u32(q+4),color;
        pc++;used++;
        switch(op) {
        case END:if(i->stage==2 || i->tick_only){i->status=1;goto done;}i->stage++;pc=p->entry[i->stage];break;
        case MOV:r[a]=r[b];break;
        case IADD:r[a].u=r[b].u+r[c].u;break;
        case ISUB:r[a].u=r[b].u-r[c].u;break;
        case IMUL:r[a].u=r[b].u*r[c].u;break;
        case IDIV:case IMOD:{
            int32_t x=r[b].i,y=r[c].i;if(!y || (x==INT32_MIN&&y==-1))goto fault;
            int32_t div=x/y,rem=x%y;
            if(rem && ((rem<0)!=(y<0))){div--;rem+=y;}
            r[a].i=op==IDIV?div:rem;break;
        }
        case IFMADD:r[a].f=(float)r[b].i*r[c].f+r[d].f;break;
        case FADD:r[a].f=r[b].f+r[c].f;break;
        case FSUB:r[a].f=r[b].f-r[c].f;break;
        case FMUL:r[a].f=r[b].f*r[c].f;break;
        case FDIV:if(r[c].f==0)goto fault;r[a].f=r[b].f/r[c].f;break;
        case FMOD:if(!finite32(r[b].f)||!finite32(r[c].f)||r[c].f==0)goto fault;r[a].f=r[b].f-floorf(r[b].f/r[c].f)*r[c].f;break;
        case ITOF:r[a].f=(float)r[b].i;break;
        case FTOI:if(!finite32(r[b].f)||r[b].f<-2147483648.0f||r[b].f>=2147483648.0f)goto fault;r[a].i=(int32_t)r[b].f;break;
        case ILT:r[a].u=r[b].i<r[c].i;break;
        case FLT:r[a].u=r[b].f<r[c].f;break;
        case EQ:r[a].u=type(p,b)==T_FLOAT?r[b].f==r[c].f:r[b].u==r[c].u;break;
        case JZ:if(!r[a].u)pc=d;break;
        case JMP:pc=d;break;
        case RGB:if(r[b].u>255||r[c].u>255||r[d].u>255)goto fault;r[a].u=r[b].u<<16|r[c].u<<8|r[d].u;break;
        case HSV:if(!finite32(r[b].f)||!finite32(r[c].f)||!finite32(r[d].f)||r[c].f<0||r[c].f>1||r[d].f<0||r[d].f>1)goto fault;r[a].u=lbc_hsv(r[b].f,r[c].f,r[d].f);break;
        case PALETTE:{if(r[b].u>=c)goto fault;const uint8_t *v=fetch(p,p->palette_offset+4*(d+r[b].u),4);if(!v)goto fault;r[a].u=lbc_u32(v);break;}
        case IMIN:r[a].i=r[b].i<r[c].i?r[b].i:r[c].i;break;
        case IMAX:r[a].i=r[b].i>r[c].i?r[b].i:r[c].i;break;
        case FMIN:r[a].f=fminf(r[b].f,r[c].f);break;
        case FMAX:r[a].f=fmaxf(r[b].f,r[c].f);break;
        case HASH:r[a].f=lbc_hash01(r[b].i,r[c].i);break;
        case STORE:if(r[a].u>=i->buffer->count)goto fault;write_rgb(i->buffer->data+3*r[a].u,r[b].u);break;
        case FILL:{
            unsigned end=i->bulk+32;if(end>i->buffer->count)end=i->buffer->count;
            for(unsigned k=i->bulk;k<end;k++)write_rgb(i->buffer->data+3*k,r[a].u);
            i->bulk=end;if(end<i->buffer->count)pc--;else i->bulk=0;break;
        }
        case HUE_RETURN:if(!finite32(r[a].f))goto fault;color=full_hue(r[a].f);goto publish_pixel;
        case HSV_RETURN:
            if(!finite32(r[a].f)||!finite32(r[b].f)||!finite32(r[d].f)||r[b].f<0||r[b].f>1||r[d].f<0||r[d].f>1)goto fault;
            color=lbc_hsv(r[a].f,r[b].f,r[d].f);goto publish_pixel;
        case RETURN:color=r[a].u;
        publish_pixel:
            write_rgb(i->buffer->data+3*(unsigned)r[1].i,color);
            if(++r[1].i==(int32_t)i->buffer->count){i->status=1;goto done;}
            pc=p->entry[2];break;
        default:goto fault;
        }
    }
    goto done;
fault:i->status=-1;
done:i->pc=pc;i->used+=used;return i->status;
}
