/* General PixelArray kernels and bounded sprite storage. The compiler lowers
 * user callbacks to bytecode; this module contains no animation behaviour. */
#include "portable_draw.h"
#include "draw.h"
#include <math.h>
#include <string.h>

static bool image(unsigned kind) { return draw_channels(kind) != 0; }
static bool owned(Portable *p, unsigned reg, PbcResourceSpec *spec) {
    if (pbc_type(p,reg)!=6 || pbc_role(p,reg)!=R_CONST) return false;
    unsigned id=pbc_initial(p,reg); if(id>=p->nresources) return false;
    *spec=pbc_spec(p,id); return true;
}
static bool writable(PbcResourceSpec s, unsigned rights) {
    return !(s.flags&2u) && (rights & (s.flags&1u ? 4u : 1u));
}
bool pbc_drawing_valid(Portable *p,const PbcCall *site,unsigned rights) {
    PbcResourceSpec first;
    if(!owned(p,site->args[0],&first)) return false;
    unsigned id=site->binding;
    if(id>=37&&id<=47) return first.kind==DRAW_BATCH && !first.flags &&
        ((id==37||id==39||id==42||id==43||id==47)?(rights&1u):true);
    if(id==32||id==33) return first.kind==DRAW_STYLE && writable(first,rights);
    if(!image(first.kind)) return false;
    if(id!=27 && !writable(first,rights)) return false;
    if(id==31||id==35) {
        PbcResourceSpec src;
        if(!owned(p,site->args[1],&src)||!image(src.kind)||((src.flags&1u)&&!(rights&4u)))return false;
    }
    if(id==50) {
        PbcResourceSpec src;
        if(!owned(p,site->args[1],&src)||(src.kind!=1&&src.kind!=11))return false;
    }
    if(id==31||id==36) {
        PbcResourceSpec style;
        if(!owned(p,site->args[id==31?2:1],&style)||style.kind!=DRAW_STYLE||style.flags)return false;
    }
    return true;
}
static PbcResource *resource(Portable *p,unsigned id) {
    return id<p->nresources && p->resources[id].generation==p->generation ? &p->resources[id] : NULL;
}
bool pbc_image_read(Portable *p,PbcResource *r,unsigned index,uint8_t pixel[5]) {
    if(!r||!image(r->kind)||index>=r->count)return false;
    memset(pixel,0,5);pixel[4]=255;
    unsigned stride=draw_stride(r->kind);
    uint8_t raw[5]={0};
    if(r->data)memcpy(raw,r->data+(size_t)index*stride,stride);
    else {
        unsigned offset=index*stride,first=offset/3,skip=offset%3,words=(skip+stride+2)/3;
        uint8_t q[12];
        if(!p->read(p->context,p->palette_offset+(r->random_state+first)*4,q,words*4))return false;
        for(unsigned c=0;c<stride;c++)raw[c]=q[((skip+c)/3)*4+(skip+c)%3];
    }
    for(unsigned c=0;c<5;c++) { int at=draw_channel(r->kind,c); if(at>=0)pixel[c]=raw[at]; }
    return true;
}
static bool write_pixel(PbcResource *r,unsigned index,const uint8_t pixel[5]) {
    if(!r||r->sealed||!r->data||index>=r->count)return false;
    uint8_t *dst=r->data+(size_t)index*draw_stride(r->kind);
    for(unsigned c=0;c<5;c++){int at=draw_channel(r->kind,c);if(at>=0)dst[at]=pixel[c];}
    return true;
}
static unsigned byte(float value) { return (unsigned)(fminf(255,fmaxf(0,value))+0.5f); }
static void to_premul(unsigned kind,uint8_t pixel[5]) {
    if(kind!=DRAW_RGBA)for(unsigned c=0;c<4;c++)pixel[c]=draw_mul255(pixel[c],pixel[4]);
}
static void from_premul(unsigned kind,uint8_t pixel[5]) {
    if(kind!=DRAW_RGBA)for(unsigned c=0;c<4;c++)pixel[c]=pixel[4]?(uint8_t)(((unsigned)pixel[c]*255+pixel[4]/2)/pixel[4]):0;
}
static void color_transform(uint8_t v[5],const DrawStyle *style,bool premul) {
    if(style->alpha==1 && style->brightness==1 && style->tint==0xffffff && style->hue==0 && style->saturation==1)return;
    unsigned old_alpha=v[4];
    if(premul)from_premul(DRAW_IMAGE_RGBA,v);
    uint32_t rgb=(uint32_t)v[0]<<16|(uint32_t)v[1]<<8|v[2];
    if(style->hue!=0 || style->saturation!=1){
        float r=v[0]/255.0f,g=v[1]/255.0f,b=v[2]/255.0f;
        float top=fmaxf(r,fmaxf(g,b)),bottom=fminf(r,fminf(g,b)),d=top-bottom,h=0;
        if(d){h=top==r?(g-b)/d:top==g?(b-r)/d+2:(r-g)/d+4;h/=6;}
        h=fmodf(h+style->hue/360.0f,1);if(h<0)h+=1;
        rgb=lbc_hsv(h,top?fminf(1,d/top*style->saturation):0,top);
    }
    for(unsigned c=0;c<3;c++)v[c]=(uint8_t)byte(((rgb>>(16-c*8))&255)*style->brightness*((style->tint>>(16-c*8))&255)/255.0f);
    v[3]=(uint8_t)byte(v[3]*style->brightness);
    v[4]=(uint8_t)byte(old_alpha*style->alpha);
    if(premul)to_premul(DRAW_IMAGE_RGBA,v);
}
static bool sample(Portable *p,PbcResource *src,int index,uint8_t v[5]) {
    memset(v,0,5);
    if(index<0||(unsigned)index>=src->count)return true;
    if(!pbc_image_read(p,src,(unsigned)index,v))return false;
    to_premul(src->kind,v);return true;
}
static void more(Portable *p,unsigned end,unsigned stop) { p->bulk=end<stop?end:0;p->native_pending=end<stop; }
bool pbc_drawing_call(Portable *p,const PbcCall *site,const PbcValue *a,PbcValue *result) {
    unsigned id=site->binding;
    PbcResource *r=resource(p,a[0].u);if(!r)return false;
    if(id>=37&&id<=47) {
        if(r->kind!=DRAW_BATCH||!r->data)return false;
        DrawBatch *batch=(DrawBatch *)r->data;
        unsigned fields=r->random_state;
        if(id==45){result->u=batch->active;return true;}
        if(id==37||id==38) {
            if(id==37&&(!isfinite(a[1].f)||a[1].f<0))return false;
            unsigned start=p->bulk?p->bulk:(id==37?0:a[1].u);
            if(start>r->count)return false;
            unsigned end=start+8;if(end>r->count)end=r->count;
            for(unsigned i=start;i<end;i++) {
                DrawInstance *instance=draw_instance(batch,i,fields);
                if((id==37&&!instance->active)||(id==38&&instance->active)) {
                    if(id==37) {
                        memset(instance,0,sizeof(*instance)+fields*4u);
                        instance->active=1;instance->born=instance->previous=p->now_us;
                        uint32_t *values=draw_instance_fields(instance);float one=1;
                        for(unsigned k=1;k<8;k++)if(k==1||k==3||k==4||k==7)memcpy(values+k,&one,4);
                        values[5]=0xffffff;++batch->active;
                    }
                    result->i=(int32_t)i;p->bulk=0;return true;
                }
            }
            if(end<r->count){p->bulk=end;p->native_pending=true;return true;}
            result->i=-1;p->bulk=0;return true;
        }
        if(a[1].u>=r->count)return false;
        DrawInstance *instance=draw_instance(batch,a[1].u,fields);
        if(id==46){result->u=instance->active;return true;}
        if(id==39){if(instance->active){instance->active=0;--batch->active;}return true;}
        if(!instance->active)return false;
        if(id==44||id==47) {
            uint64_t since=id==44?instance->born:instance->previous;
            if(!isfinite(a[2].f)||p->now_us<since)return false;
            result->f=(float)((p->now_us-since)/1000000.0);
            if(id==47)instance->previous=p->now_us;
            return isfinite(result->f);
        }
        if(a[2].u>=fields)return false;
        bool floating;
        /* The type mask is fixed at admission, stored in the resource spec. */
        PbcResourceSpec spec=pbc_spec(p,a[0].u);PbcExpression mask=pbc_expression(p,spec.stream);
        floating=(mask.value&(1u<<a[2].u))!=0;
        if(floating!=(id==41||id==43))return false;
        uint32_t *values=draw_instance_fields(instance);
        if(id==40||id==41){result->u=values[a[2].u];return true;}
        if(id==43&&!isfinite(a[3].f))return false;
        values[a[2].u]=a[3].u;return true;
    }
    if(id==32||id==33) {
        if(r->kind!=DRAW_STYLE||!r->data)return false;
        DrawStyle *s=(DrawStyle *)r->data;
        if(id==32){if(!isfinite(a[1].f)||!isfinite(a[2].f)||!isfinite(a[3].f)||a[2].f==0)return false;s->translate=a[1].f;s->scale=a[2].f;s->origin=a[3].f;}
        else{if(!isfinite(a[1].f)||a[1].f<0||a[1].f>1||!isfinite(a[2].f)||a[2].f<0||a[3].u>0xffffff||!isfinite(a[4].f)||!isfinite(a[5].f)||a[5].f<0)return false;s->alpha=a[1].f;s->brightness=a[2].f;s->tint=a[3].u;s->hue=a[4].f;s->saturation=a[5].f;}
        return true;
    }
    if(!image(r->kind))return false;
    if(id==27){if(a[1].u>4)return false;result->u=draw_channel(r->kind,a[1].u)>=0;return true;}
    if(r->sealed||!r->data)return false;
    unsigned start=0,stop=r->count;
    if(id==28){start=a[3].u;stop=a[4].u;if(a[1].u>4||draw_channel(r->kind,a[1].u)<0||a[2].u>255)return false;}
    if(id==29||id==30){start=a[2].u;stop=a[3].u;if(draw_channel(r->kind,4)<0||!isfinite(a[1].f)||a[1].f<0||(id==29&&a[1].f>1))return false;}
    if(id==34){start=a[3].u;stop=a[4].u;if(a[1].u>0xffffff||a[2].u>255)return false;}
    if(id==36){start=a[2].u;stop=a[3].u;}
    if(start>stop||stop>r->count)return false;
    PbcResource *src=NULL,*style=NULL;
    if(id==31||id==35){src=resource(p,a[1].u);if(!src||!image(src->kind))return false;if(src==r){p->bulk=0;return id==35;}}
    if(id==35&&(src->count!=r->count||src->kind!=r->kind))return false;
    if(id==31||id==36){style=resource(p,a[id==31?2:1].u);if(!style||style->kind!=DRAW_STYLE||!style->data)return false;}
    if(id==31&&a[3].u>1)return false;
    if(id==31){
        DrawStyle *s=(DrawStyle *)style->data;
        float left=s->translate+(-1-s->origin)*s->scale,right=s->translate+((float)src->count-s->origin)*s->scale;
        float lo=fminf(left,right),hi=fmaxf(left,right);
        if(hi<0||lo>(float)r->count){p->bulk=0;return true;}
        start=(unsigned)floorf(fminf((float)r->count,fmaxf(0,lo)));
        stop=(unsigned)fminf((float)r->count,ceilf(fmaxf(0,hi))+1);
    }
    if(id==51){if(a[1].u>=r->count||a[2].u>0xffffff||a[3].u>255)return false;start=a[1].u;stop=start+1;}
    if(id==48&&(a[1].u>0xffffff||a[2].u>0xffffff))return false;
    if(id==49&&(!isfinite(a[1].f)||!isfinite(a[2].f)))return false;
    PbcResource *palette=id==50?resource(p,a[1].u):NULL;
    if(id==50&&(!palette||!palette->data||!palette->count||!isfinite(a[2].f)||!isfinite(a[3].f)))return false;
    unsigned cursor=p->bulk?p->bulk:start,end=cursor+32;if(end>stop)end=stop;
    for(unsigned i=cursor;i<end;i++) {
        uint8_t v[5];if(!pbc_image_read(p,r,i,v))return false;
        if(id==28||id==29||id==30) {
            unsigned channel=id==28?a[1].u:4;
            unsigned value=id==28?a[2].u:byte((id==29?255:v[4])*a[1].f);
            if(r->kind==DRAW_RGBA){from_premul(DRAW_IMAGE_RGBA,v);v[channel]=(uint8_t)value;to_premul(DRAW_IMAGE_RGBA,v);}
            else v[channel]=(uint8_t)value;
        }else if(id==34) {
            v[0]=(uint8_t)(a[1].u>>16);v[1]=(uint8_t)(a[1].u>>8);v[2]=(uint8_t)a[1].u;v[3]=0;v[4]=(uint8_t)a[2].u;
            if(r->kind==DRAW_RGBA)to_premul(DRAW_IMAGE_RGBA,v);
        }else if(id>=48&&id<=50){
            uint32_t color;
            if(id==49){float hue=a[1].f+i*a[2].f;if(!isfinite(hue))return false;color=draw_colorwheel(hue);}
            else{
                uint32_t left=a[1].u,right=a[2].u;unsigned mix=r->count>1?(i*255u+(r->count-1)/2)/(r->count-1):0;
                if(id==50){
                    float phase=a[2].f+i*a[3].f;if(!isfinite(phase))return false;
                    phase=fmodf(phase,(float)palette->count);if(phase<0)phase+=palette->count;
                    unsigned index=(unsigned)phase;mix=(unsigned)((phase-index)*255+0.5f);
                    memcpy(&left,palette->data+index*4,4);memcpy(&right,palette->data+((index+1)%palette->count)*4,4);
                    if(left>0xffffff||right>0xffffff)return false;
                }
                color=0;for(unsigned c=0;c<3;c++){unsigned shift=16-c*8;unsigned value=(((left>>shift)&255)*(255-mix)+((right>>shift)&255)*mix+127)/255;color=(color<<8)|value;}
            }
            v[0]=(uint8_t)(color>>16);v[1]=(uint8_t)(color>>8);v[2]=(uint8_t)color;v[3]=0;v[4]=255;
        }else if(id==51){
            uint8_t point[5]={(uint8_t)(a[2].u>>16),(uint8_t)(a[2].u>>8),(uint8_t)a[2].u,0,(uint8_t)a[3].u};
            to_premul(DRAW_IMAGE_RGBA,point);to_premul(r->kind,v);
            for(unsigned c=0;c<5;c++)v[c]=(uint8_t)(point[c]+draw_mul255(v[c],255-point[4]));
            from_premul(r->kind,v);
        }else if(id==35){if(!pbc_image_read(p,src,i,v))return false;}
        else if(id==36)color_transform(v,(DrawStyle *)style->data,r->kind==DRAW_RGBA);
        else if(id==31) {
            DrawStyle *s=(DrawStyle *)style->data;
            float x=((float)i-s->translate)/s->scale+s->origin;
            if(!isfinite(x))return false;
            if(x < -1 || x > (float)src->count)continue;
            uint8_t left[5],right[5];
            int base=(int)floorf(x);
            if(a[3].u==0){if(!sample(p,src,(int)floorf(x+0.5f),left))return false;}
            else {
                if(!sample(p,src,base,left)||!sample(p,src,base+1,right))return false;
                float mix=x-floorf(x);
                for(unsigned c=0;c<5;c++)left[c]=(uint8_t)byte(left[c]*(1-mix)+right[c]*mix);
            }
            color_transform(left,s,true);to_premul(r->kind,v);
            for(unsigned c=0;c<5;c++)v[c]=(uint8_t)(left[c]+draw_mul255(v[c],255-left[4]));
            from_premul(r->kind,v);
        }else return false;
        if(!write_pixel(r,i,v))return false;
    }
    more(p,end,stop);return true;
}
