#include "native_math.h"
#include <math.h>
#include <string.h>
#include <float.h>
_Static_assert(sizeof(double)==8&&DBL_MANT_DIG==53&&DBL_MAX_EXP==1024&&FLT_RADIX==2,
               "native math requires IEEE binary64 intermediates");

/* Copy the exact binary32 value into a normal binary64 value, including f32
 * subnormals. This avoids target conversion routines that flush subnormals. */
static double wide(uint32_t word){
    unsigned exponent=(word>>23)&255u;uint32_t mantissa=word&0x7fffffu;
    int e=(int)exponent-127;
    if(!exponent){
        if(!mantissa){uint64_t bits=(uint64_t)(word&0x80000000u)<<32;double v;memcpy(&v,&bits,8);return v;}
        e=-126;while(!(mantissa&0x800000u)){mantissa<<=1;e--;}
    }
    uint64_t bits=((uint64_t)(word&0x80000000u)<<32)|((uint64_t)(e+1023)<<52)|((uint64_t)(mantissa&0x7fffffu)<<29);
    double value;memcpy(&value,&bits,8);return value;
}
static uint64_t rounded(uint64_t value,unsigned shift){
    if(shift>=64)return 0;
    uint64_t whole=value>>shift,rest=value&((UINT64_C(1)<<shift)-1),half=UINT64_C(1)<<(shift-1);
    return whole+(rest>half||(rest==half&&(whole&1)));
}
/* Round once to binary32, nearest/even, with gradual underflow. */
static bool narrow(double value,uint32_t *out){
    uint64_t bits;memcpy(&bits,&value,8);
    unsigned exponent=(unsigned)((bits>>52)&2047u);uint32_t sign=(uint32_t)(bits>>32)&0x80000000u;
    if(exponent==2047)return false;
    if(!exponent){*out=sign;return true;}
    int e=(int)exponent-1023;uint64_t mantissa=(bits&UINT64_C(0xfffffffffffff))|UINT64_C(0x10000000000000);
    if(e < -126){*out=sign|(uint32_t)rounded(mantissa,(unsigned)(-126-e+29));return true;}
    mantissa=rounded(mantissa,29);
    if(mantissa==UINT64_C(0x1000000)){mantissa>>=1;e++;}
    if(e>127)return false;
    *out=sign|((uint32_t)(e+127)<<23)|((uint32_t)mantissa&0x7fffffu);return true;
}
static uint32_t integral(uint32_t bits,unsigned op){
    unsigned exponent=(bits>>23)&255u;bool negative=(bits>>31)!=0;
    if(exponent>=150||(bits&0x7fffffffu)==0)return bits;
    if(exponent<127){
        if((op==LVM_FLOOR&&negative)||(op==LVM_CEIL&&!negative))return (bits&0x80000000u)|0x3f800000u;
        return bits&0x80000000u;
    }
    unsigned shift=150-exponent;uint32_t mask=(1u<<shift)-1,result=bits&~mask;
    if((bits&mask)&&((op==LVM_FLOOR&&negative)||(op==LVM_CEIL&&!negative)))result+=1u<<shift;
    return result;
}
static uint32_t root(uint32_t bits){
    if(!(bits&0x7fffffffu))return bits;
    int e=(int)((bits>>23)&255u)-127;uint32_t m=bits&0x7fffffu;
    if(e==-127){e=-126;while(!(m&0x800000u)){m<<=1;e--;}}
    else m|=0x800000u;
    unsigned odd=(unsigned)e&1u;uint64_t value=(uint64_t)m<<(23+odd),remainder=value,result=0;
    for(uint64_t bit=UINT64_C(1)<<46;bit;bit>>=2){
        if(remainder>=result+bit){remainder-=result+bit;result=(result>>1)+bit;}
        else result>>=1;
    }
    /* The squared midpoint is q*q+q+1/4, so integer radicands never tie. */
    if(remainder>result)result++;
    e=(e-(int)odd)/2;
    if(result==UINT64_C(0x1000000)){result>>=1;e++;}
    return ((uint32_t)(e+127)<<23)|((uint32_t)result&0x7fffffu);
}
static uint32_t ordered(uint32_t bits){return bits&0x80000000u?~bits:bits^0x80000000u;}
static bool odd_integer(uint32_t bits){
    unsigned e=(bits>>23)&255u;
    return e>=127&&e<=150&&(((bits|0x800000u)>>(150-e))&1u);
}
bool lvm_f32_eval(unsigned op,uint32_t left,uint32_t right,uint32_t *out){
    if((left&0x7f800000u)==0x7f800000u||
       ((op==LVM_POW||op==LVM_MIN||op==LVM_MAX)&&(right&0x7f800000u)==0x7f800000u))return false;
    uint32_t result;double value;
    switch(op){
    case LVM_ABS:result=left&0x7fffffffu;break;
    case LVM_FLOOR:case LVM_CEIL:case LVM_TRUNC:result=integral(left,op);break;
    case LVM_SQRT:if((left>>31)&&(left&0x7fffffffu))return false;result=root(left);break;
    case LVM_MIN:case LVM_MAX:
        if(!((left|right)&0x7fffffffu))result=op==LVM_MIN?(left|right):(left&right);
        else result=(ordered(left)<ordered(right))==(op==LVM_MIN)?left:right;
        break;
    case LVM_SIN:value=sin(wide(left));goto computed;
    case LVM_COS:value=cos(wide(left));goto computed;
    case LVM_LOG:if((left>>31)||!left)return false;value=log(wide(left));goto computed;
    case LVM_EXP:{
        double x=wide(left);
        /* Generous bounds outside the f32 overflow/round-to-zero thresholds
         * keep libm away from binary64 range errors and shared errno writes. */
        if(x>90.0)return false;
        if(x< -105.0){result=0;break;}
        value=exp(x);goto computed;
    }
    case LVM_POW:{
        if(!(right&0x7fffffffu)){result=0x3f800000u;break;}
        if(!(left&0x7fffffffu)){
            if(right>>31)return false;
            result=(left&0x80000000u)&&odd_integer(right)&&integral(right,LVM_TRUNC)==right?0x80000000u:0;break;
        }
        if((left>>31)&&integral(right,LVM_TRUNC)!=right)return false;
        double base=wide(left&0x7fffffffu),exponent=wide(right);
        /* Every nonzero f32 base to an exponent in [-6,6] stays normal in
         * binary64. For larger exponents, coarse logarithmic bounds have over
         * one natural-log unit of slack from either f32 threshold. Only the
         * safely bounded middle interval reaches pow; its result still gets
         * the exact f32 narrowing check below. This also prevents libm's
         * range-error path from racing on bare-metal errno between cores. */
        if((right&0x7fffffffu)>0x40c00000u){
            double magnitude=log(base)*exponent;
            if(magnitude>90.0)return false;
            if(magnitude< -105.0){result=(left>>31)&&odd_integer(right)?0x80000000u:0;break;}
        }
        value=pow(base,exponent);
        if((left>>31)&&odd_integer(right))value=-value;
        goto computed;
    }
    default:return false;
    }
    *out=result;return true;
computed:
    if(!narrow(value,&result))return false;
    *out=result;return true;
}
