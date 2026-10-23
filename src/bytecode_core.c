#include "bytecode_core.h"
#include "config.h"
#include "numeric.h"
#include "native_math.h"
#include <math.h>
#include "numeric_metadata.inc"
LbcNumericSignature lbc_numeric_signature(unsigned op){
    return op>=QADD&&op<=FPOW?lbc_numeric_metadata(op).signature:(LbcNumericSignature){0};
}
bool lbc_numeric_eval(unsigned op,uint32_t left,uint32_t right,uint32_t *out){
    if(op>=FABS&&op<=FPOW)return lvm_f32_eval(op-FABS,left,right,out);
    if(op==FMIN||op==FMAX)return lvm_f32_eval(op==FMIN?LVM_MIN:LVM_MAX,left,right,out);
    int32_t a=lvm_i32_from_bits(left),b=lvm_i32_from_bits(right),value;
    switch(op){
    case QADD:value=lvm_q16_add(a,b);break;
    case QSUB:value=lvm_q16_sub(a,b);break;
    case QMUL:value=lvm_q16_mul(a,b);break;
    case QDIV:if(!lvm_q16_div(a,b,&value))return false;break;
    case QMOD:if(!lvm_q16_mod(a,b,&value))return false;break;
    case QNEG:value=lvm_q16_neg(a);break;
    case QMIN:value=lvm_q16_min(a,b);break;
    case QMAX:value=lvm_q16_max(a,b);break;
    case QLT:value=lvm_q16_lt(a,b);break;
    case ITOQ:if(!lvm_q16_from_i32(a,&value))return false;break;
    case QTOI:value=lvm_q16_to_i32(a);break;
    case FTOQ:if(!lvm_q16_from_f32_bits(left,&value))return false;break;
    case QTOF:*out=lvm_q16_to_f32_bits(a);return true;
    case QFROMBITS:case QBITS:*out=left;return true;
    case QSQRT:if(!lvm_q16_sqrt(a,&value))return false;break;
    case IAND:value=lvm_i32_and(a,b);break;
    case IOR:value=lvm_i32_or(a,b);break;
    case IXOR:value=lvm_i32_xor(a,b);break;
    case INOT:value=lvm_i32_not(a);break;
    case ISHL:if(!lvm_i32_shl(a,b,&value))return false;break;
    case ISHR:if(!lvm_i32_shr(a,b,&value))return false;break;
    default:return false;
    }
    *out=lvm_i32_bits(value);return true;
}
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
LIGHTVM_DRAW_CODE uint32_t lbc_hsv(float h,float s,float v) {
    if(!lbc_finite(h)||!lbc_finite(s)||!lbc_finite(v)||s<0||s>1||v<0||v>1)return 0;
    uint32_t hue=(uint32_t)((h-floorf(h))*65536.0f+0.5f)&65535;
    return quantized_hsv(hue,(uint32_t)(s*255.0f+0.5f),(uint32_t)(v*255.0f+0.5f));
}
float lbc_hash01(int32_t index,int32_t seed) {
    /* Matches the original sample's polynomial hash, with explicit i32 inputs. */
    int64_t x=((int64_t)index+(int64_t)seed*131)%65521;if(x<0)x+=65521;
    x=(x*x*17+x*31+101)%65521;
    return (float)x/65521.0f;
}
