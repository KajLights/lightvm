#include "bytecode_core.h"
#include <math.h>
#include <float.h>
_Static_assert(sizeof(float)==4 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,"LBC1 requires IEEE binary32");
static inline bool finite32(float f) { return isfinite(f); }
uint32_t lbc_u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
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
