/* Independent wide-integer reference for the common, allocation-free kernels. */
#include "draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if(!(value)){fprintf(stderr,"line %d: %s\n",__LINE__,#value);exit(1);} } while(0)

static unsigned over(unsigned lower,unsigned upper,unsigned alpha,uint32_t opacity){
    uint64_t denominator=255ull*DRAW_OPACITY_ONE;
    uint64_t numerator=(uint64_t)upper*opacity*255u+(uint64_t)lower*(denominator-(uint64_t)alpha*opacity);
    return (unsigned)((numerator+denominator/2)/denominator);
}
static unsigned scale(unsigned value,unsigned alpha){return (value*alpha+127u)/255u;}
static unsigned blend(unsigned lower,unsigned upper,unsigned alpha,uint32_t opacity,DrawBlend mode){
    if(mode==DRAW_BLEND_OVER)return over(lower,upper,alpha,opacity);
    unsigned paint=(unsigned)(((uint64_t)upper*opacity+32768u)/DRAW_OPACITY_ONE);
    unsigned a=(unsigned)(((uint64_t)alpha*opacity+32768u)/DRAW_OPACITY_ONE);
    if(mode==DRAW_BLEND_ADD)return lower+paint>255?255:lower+paint;
    if(mode==DRAW_BLEND_SCREEN)return 255-scale(255-lower,255-paint);
    return scale(lower,255-a+paint);
}
static uint32_t rng=19;
static unsigned random_byte(void){rng=rng*1664525u+1013904223u;return rng>>24;}

static void opaque_precision(void){
    const uint32_t weights[]={0,1,257,4097,6553,12345,22938,32767,32768,32897,49151,65535,DRAW_OPACITY_ONE};
    for(unsigned a=0;a<256;a++)for(unsigned b=0;b<256;b++){
        uint8_t lower[4]={(uint8_t)a,(uint8_t)a,(uint8_t)a,255},upper[4]={(uint8_t)b,(uint8_t)b,(uint8_t)b,255};
        for(unsigned i=0;i<sizeof(weights)/sizeof(*weights);i++){
            unsigned expected=over(a,b,255,weights[i]);uint8_t rgb[4],rgba[4];
            draw_composite(rgb,4,lower,3,upper,3,1,weights[i],DRAW_BLEND_OVER);
            draw_composite(rgba,4,lower,4,upper,4,1,weights[i],DRAW_BLEND_OVER);
            CHECK(rgb[0]==expected&&rgb[1]==expected&&rgb[2]==expected&&rgb[3]==255);
            CHECK(!memcmp(rgb,rgba,4));
        }
    }
    /* Every possible 8-bit opacity retains exact byte scaling; increasing
       compositor precision does not alter add/screen/multiply preparation. */
    for(unsigned opacity=0;opacity<256;opacity++)for(unsigned value=0;value<256;value++){
        uint8_t lower[4]={0},upper[4]={(uint8_t)value,0,0,255};
        draw_blend(lower,upper,opacity,DRAW_BLEND_OVER);
        CHECK(lower[0]==scale(value,opacity));
    }
    CHECK(draw_opacity_float(0.35f)==22938);
    CHECK(draw_opacity_float(0)==0&&draw_opacity_float(1)==DRAW_OPACITY_ONE);
}
static void alpha_and_formats(void){
    for(unsigned n=0;n<100000;n++){
        unsigned channels=n&1?3:4;
        uint8_t lower[5],upper[5],actual[5];
        lower[channels]=(uint8_t)random_byte();upper[channels]=(uint8_t)random_byte();
        for(unsigned c=0;c<channels;c++){
            lower[c]=(uint8_t)(random_byte()%(lower[channels]+1u));
            upper[c]=(uint8_t)(random_byte()%(upper[channels]+1u));
        }
        uint32_t opacity=((uint32_t)random_byte()<<8)|random_byte();
        if(n%17==0)opacity=DRAW_OPACITY_ONE;
        if(n%19==0)opacity=0;
        memcpy(actual,lower,channels+1);
        draw_over_channels(actual,upper,channels,opacity);
        for(unsigned c=0;c<=channels;c++)CHECK(actual[c]==over(lower[c],upper[c],upper[channels],opacity));
        for(unsigned c=0;c<channels;c++)CHECK(actual[c]<=actual[channels]);
        if(channels==3){
            uint8_t result[4];
            draw_composite(result,4,lower,4,upper,4,1,opacity,DRAW_BLEND_OVER);
            CHECK(!memcmp(result,actual,4));
        }
    }
    for(unsigned lower_stride=3;lower_stride<=4;lower_stride++)for(unsigned upper_stride=3;upper_stride<=4;upper_stride++)for(unsigned out_stride=3;out_stride<=4;out_stride++){
        uint8_t lower[67*4],upper[67*4],output[67*4+2],alias[67*4];
        for(unsigned mode=0;mode<DRAW_BLEND_COUNT;mode++)for(unsigned w=0;w<256;w+=17){
            uint32_t opacity=draw_opacity(w);
            memset(output,0x5a,sizeof(output));
            for(unsigned i=0;i<67;i++){
                unsigned alpha=random_byte();
                if(upper_stride==4)upper[i*4+3]=(uint8_t)alpha;else alpha=255;
                if(lower_stride==4)lower[i*4+3]=255;
                for(unsigned c=0;c<3;c++){lower[i*lower_stride+c]=(uint8_t)random_byte();upper[i*upper_stride+c]=(uint8_t)(random_byte()%(alpha+1));}
            }
            draw_composite(output+1,out_stride,lower,lower_stride,upper,upper_stride,67,opacity,(DrawBlend)mode);
            CHECK(output[0]==0x5a&&output[1+67*out_stride]==0x5a);
            for(unsigned i=0;i<67;i++){
                unsigned alpha=upper_stride==4?upper[i*4+3]:255;
                for(unsigned c=0;c<3;c++)CHECK(output[1+i*out_stride+c]==blend(lower[i*lower_stride+c],upper[i*upper_stride+c],alpha,opacity,(DrawBlend)mode));
                if(out_stride==4)CHECK(output[1+i*4+3]==255);
            }
            if(out_stride==lower_stride){
                memcpy(alias,lower,67*lower_stride);draw_composite(alias,out_stride,alias,lower_stride,upper,upper_stride,67,opacity,(DrawBlend)mode);
                CHECK(!memcmp(alias,output+1,67*out_stride));
            }
            if(out_stride==upper_stride){
                memcpy(alias,upper,67*upper_stride);draw_composite(alias,out_stride,lower,lower_stride,alias,upper_stride,67,opacity,(DrawBlend)mode);
                CHECK(!memcmp(alias,output+1,67*out_stride));
            }
        }
    }
    uint8_t out[4],source[4]={9,18,27,255};
    draw_composite(out,4,NULL,3,source,3,1,DRAW_OPACITY_ONE,DRAW_BLEND_OVER);CHECK(!memcmp(out,source,4));
    draw_composite(out,4,source,4,NULL,4,1,DRAW_OPACITY_ONE,DRAW_BLEND_OVER);CHECK(!memcmp(out,source,4));
    draw_composite(out,4,source,3,NULL,3,1,DRAW_OPACITY_ONE,DRAW_BLEND_OVER);CHECK(out[0]==0&&out[1]==0&&out[2]==0&&out[3]==255);
    uint8_t edge[4]={255,255,255,255};
    draw_over_channels(edge,source,3,DRAW_OPACITY_ONE);CHECK(!memcmp(edge,source,4));
    for(unsigned alpha=0;alpha<256;alpha++)for(unsigned opacity=0;opacity<256;opacity++){
        /* White over white hits the largest valid intermediate values. */
        uint8_t dst[4]={255,255,255,255},src[4]={(uint8_t)alpha,(uint8_t)alpha,(uint8_t)alpha,(uint8_t)alpha};
        draw_over_channels(dst,src,3,draw_opacity(opacity));
        CHECK(dst[0]==255&&dst[1]==255&&dst[2]==255&&dst[3]==255);
    }
}
int main(void){opaque_precision();alpha_and_formats();return 0;}
