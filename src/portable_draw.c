/* General PixelArray kernels and bounded sprite storage. The compiler lowers
 * user callbacks to bytecode; this module contains no animation behaviour. */
#include "portable_draw.h"
#include "draw.h"
#include "render_executor.h"
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
    if(id==53||id==54)return p->profile==4&&first.kind==DRAW_RGBA&&first.flags==1&&(rights&4u);
    if(id>=37&&id<=47) return first.kind==DRAW_BATCH && !first.flags &&
        ((id==37||id==39||id==42||id==43||id==47)?(rights&1u):true);
    if(id==32||id==33) return first.kind==DRAW_STYLE && writable(first,rights);
    if(!image(first.kind)) return false;
    if(id!=27 && !writable(first,rights)) return false;
    if(id==31||id==35||id==52) {
        PbcResourceSpec src;
        if(!owned(p,site->args[1],&src)||!image(src.kind)||((src.flags&1u)&&!(rights&4u)))return false;
    }
    if(id==50) {
        PbcResourceSpec src;
        if(!owned(p,site->args[1],&src)||(src.kind!=1&&src.kind!=11))return false;
    }
    if(id==31||id==36||id==52) {
        PbcResourceSpec style;
        if(!owned(p,site->args[id==36?1:2],&style)||style.kind!=DRAW_STYLE||style.flags)return false;
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
/* Add a filtered source sample before rounding. Periodic copies meet at the
 * seam, so accumulate their premultiplied weights and blend exactly once. */
static bool sample_add(Portable *p,PbcResource *src,double x,bool linear,float sum[5]) {
    if(x < -1.0 || x > (double)src->count)return true;
    uint8_t left[5],right[5];
    if(!linear) {
        if(!sample(p,src,(int)floor(x+0.5),left))return false;
        for(unsigned c=0;c<5;c++)sum[c]+=left[c];
    }else{
        int base=(int)floor(x);float mix=(float)(x-floor(x));
        if(!sample(p,src,base,left)||!sample(p,src,base+1,right))return false;
        for(unsigned c=0;c<5;c++)sum[c]+=left[c]*(1-mix)+right[c]*mix;
    }
    return true;
}
/* This view is frozen for one operation. Resource/style/RNG mutation stays
 * ordered in the interpreter; workers write only their destination pixels. */
typedef struct {
    Portable *p;PbcResource *r,*src,*style,*palette;const PbcValue *a;
    unsigned id;double period,wrapped_translate,wrapped_origin;
    bool integer_mapping;
    int offset;
} DrawKernel;
/* Keep solid-span scratch out of the transformed-image loop's register/stack
   allocation. These operations have no position-dependent sampling. */
static __attribute__((noinline)) void fill_pixels(PbcResource *r,const PbcValue *a,unsigned first,unsigned end){
    uint8_t v[5]={(uint8_t)(a[1].u>>16),(uint8_t)(a[1].u>>8),(uint8_t)a[1].u,0,(uint8_t)a[2].u},packed[5];
    if(r->kind==DRAW_RGBA)to_premul(DRAW_IMAGE_RGBA,v);
    unsigned stride=draw_stride(r->kind);
    for(unsigned c=0;c<5;c++){int at=draw_channel(r->kind,c);if(at>=0)packed[at]=v[c];}
    uint8_t *out=r->data+(size_t)first*stride;
    for(unsigned i=first;i<end;i++,out+=stride)memcpy(out,packed,stride);
}
static __attribute__((noinline)) bool transform_pixels(Portable *p,PbcResource *r,const DrawStyle *style,unsigned first,unsigned end){
    uint8_t previous[5],transformed[5];
    for(unsigned i=first;i<end;i++){
        uint8_t v[5];if(!pbc_image_read(p,r,i,v))return false;
        /* Reuse the exact floating-point/premultiplication result only for
           identical input pixels under this chunk's immutable style. */
        if(i==first||memcmp(v,previous,sizeof(v))){
            memcpy(previous,v,sizeof(v));color_transform(v,style,r->kind==DRAW_RGBA);
            memcpy(transformed,v,sizeof(v));
        }else memcpy(v,transformed,sizeof(v));
        if(!write_pixel(r,i,v))return false;
    }
    return true;
}
static bool draw_pixels(DrawKernel *k,unsigned first,unsigned end){
    Portable *p=k->p;PbcResource *r=k->r,*src=k->src,*style=k->style,*palette=k->palette;
    const PbcValue *a=k->a;unsigned id=k->id;
    double period=k->period,wrapped_translate=k->wrapped_translate,wrapped_origin=k->wrapped_origin;
    if(id==34){
        /* Fill overwrites every channel. Pack once, without reading pixels or
           repeating colour conversion for each destination. */
        fill_pixels(r,a,first,end);return true;
    }
    if(id==36)return transform_pixels(p,r,(DrawStyle *)style->data,first,end);
    for(unsigned i=first;i<end;i++) {
        uint8_t v[5];if(!pbc_image_read(p,r,i,v))return false;
        if(id==28||id==29||id==30) {
            unsigned channel=id==28?a[1].u:4;
            unsigned value=id==28?a[2].u:byte((id==29?255:v[4])*a[1].f);
            if(r->kind==DRAW_RGBA){from_premul(DRAW_IMAGE_RGBA,v);v[channel]=(uint8_t)value;to_premul(DRAW_IMAGE_RGBA,v);}
            else v[channel]=(uint8_t)value;
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
            draw_over_channels(v,point,4,DRAW_OPACITY_ONE);
            from_premul(r->kind,v);
        }else if(id==35){if(!pbc_image_read(p,src,i,v))return false;}
        else if(id==31||id==52) {
            DrawStyle *s=(DrawStyle *)style->data;
            uint8_t left[5],right[5];
            if(id==52){
                double x=fmod(((double)i-wrapped_translate)/(double)s->scale+wrapped_origin,period);
                if(x<0)x+=period;
                float sum[5]={0};
                if(!sample_add(p,src,x,a[3].u!=0,sum)||!sample_add(p,src,x-period,a[3].u!=0,sum))return false;
                for(unsigned c=0;c<5;c++)left[c]=(uint8_t)byte(sum[c]);
            }else if(k->integer_mapping){
                /* Under these bounds, each original float operation is an
                   exact integer. Preserve package reads (and their faults),
                   including the zero-weight neighbour of linear sampling. */
                int x=(int)i+k->offset;
                if(x < -1 || x > (int)src->count)continue;
                if(!sample(p,src,x,left))return false;
                if(a[3].u&&!src->data&&!sample(p,src,x+1,right))return false;
            }else{
                float x=((float)i-s->translate)/s->scale+s->origin;
                if(!isfinite(x))return false;
                if(x < -1 || x > (float)src->count)continue;
                int base=(int)floorf(x);
                if(a[3].u==0){if(!sample(p,src,(int)floorf(x+0.5f),left))return false;}
                else {
                    if(!sample(p,src,base,left)||!sample(p,src,base+1,right))return false;
                    float mix=x-floorf(x);
                    for(unsigned c=0;c<5;c++)left[c]=(uint8_t)byte(left[c]*(1-mix)+right[c]*mix);
                }
            }
            color_transform(left,s,true);to_premul(r->kind,v);
            draw_over_channels(v,left,4,DRAW_OPACITY_ONE);
            from_premul(r->kind,v);
        }else return false;
        if(!write_pixel(r,i,v))return false;
    }
    return true;
}
typedef struct { DrawKernel *kernel;unsigned first,end;bool success; } DrawTask;
static void draw_task(void *context){
    DrawTask *task=context;task->success=draw_pixels(task->kernel,task->first,task->end);
}
static void more(Portable *p,unsigned end,unsigned stop) { p->bulk=end<stop?end:0;p->native_pending=end<stop; }
bool pbc_drawing_call(Portable *p,const PbcCall *site,const PbcValue *a,PbcValue *result) {
    unsigned id=site->binding;
    PbcResource *r=resource(p,a[0].u);if(!r)return false;
    if(id==53||id==54){
        if(p->profile!=4||r->kind!=DRAW_RGBA||r->sealed||!r->data||!p->output||r->count!=p->output->count)return false;
        unsigned start=a[2].u,stop=a[3].u;
        if(start>stop||stop>r->count)return false;
        uint32_t weight=0;
        if(id==53){
            if(!isfinite(a[1].f)||a[1].f<0||a[1].f>1)return false;
            weight=draw_opacity_float(a[1].f);
        }else if(a[1].u>1)return false;
        unsigned cursor=p->bulk?p->bulk:start,end=cursor+32;if(end>stop)end=stop;
        const uint8_t *left=p->composition_frames[0],*right=p->composition_frames[1];
        if(id==54){left=p->composition_frames[a[1].u];right=left;weight=0;}
        draw_composite_rgb(r->data+(size_t)cursor*4,4,left?left+(size_t)cursor*3:NULL,
                           right?right+(size_t)cursor*3:NULL,end-cursor,weight);
        more(p,end,stop);return true;
    }
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
    if(id==31||id==35||id==52){src=resource(p,a[1].u);if(!src||!image(src->kind))return false;if(src==r){p->bulk=0;return id==35;}}
    if(id==35&&(src->count!=r->count||src->kind!=r->kind))return false;
    if(id==31||id==36||id==52){style=resource(p,a[id==36?1:2].u);if(!style||style->kind!=DRAW_STYLE||!style->data)return false;}
    if((id==31||id==52)&&a[3].u>1)return false;
    double period=0,wrapped_translate=0,wrapped_origin=0;
    if(id==52){
        DrawStyle *s=(DrawStyle *)style->data;
        if(!isfinite(s->translate)||!isfinite(s->origin)||!isfinite(s->scale)||s->scale==0)return false;
        if(!r->count || (double)src->count*fabs((double)s->scale)>(double)r->count)return false;
        period=(double)r->count/fabs((double)s->scale);
        wrapped_translate=fmod((double)s->translate,(double)r->count);
        wrapped_origin=fmod((double)s->origin,period);
    }
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
    /* Large chunks are worthwhile only when they actually split. Two busy
       VM workers need small serial chunks to keep their time slices balanced. */
    bool parallel=p->output&&r->data==p->output->data&&(!src||src->data)&&render_executor_can_split();
    unsigned cursor=p->bulk?p->bulk:start,end=cursor+(parallel?128u:32u);if(end>stop)end=stop;
    DrawKernel kernel={.p=p,.r=r,.src=src,.style=style,.palette=palette,.a=a,.id=id,
        .period=period,.wrapped_translate=wrapped_translate,.wrapped_origin=wrapped_origin};
    if(id==31){
        DrawStyle *s=(DrawStyle *)style->data;
        kernel.integer_mapping=s->scale==1&&r->count<=65535&&src->count<=65535&&
            fabsf(s->translate)<=65535&&fabsf(s->origin)<=65535&&
            truncf(s->translate)==s->translate&&truncf(s->origin)==s->origin;
        if(kernel.integer_mapping)kernel.offset=(int)s->origin-(int)s->translate;
    }
    unsigned middle=parallel&&end-cursor>=64?cursor+(end-cursor)/2:end;
    DrawTask tasks[2]={{&kernel,cursor,middle,false},{&kernel,middle,end,false}};
    RenderJob jobs[2]={{draw_task,&tasks[0],0},{draw_task,&tasks[1],0}};
    unsigned count=middle<end?2:1;render_execute(jobs,count);
    for(unsigned n=0;n<count;n++)if(!tasks[n].success)return false;
    more(p,end,stop);return true;
}
