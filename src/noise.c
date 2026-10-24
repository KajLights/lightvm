#include "noise.h"
#include <string.h>
#include <float.h>

_Static_assert(sizeof(float)==4 && FLT_MANT_DIG==24 && sizeof(double)==8 && DBL_MANT_DIG==53,
               "noise requires IEEE binary32 and binary64");
#include "noise_metadata.inc"

/* Independently implemented gradient noise. No permutation state, RNG stream,
 * initialization or shared mutable tables. Unsigned arithmetic wraps by design.
 * Axis salts and the avalanche are part of the LightVM sampling contract. */
static uint32_t avalanche(uint32_t h){
    h^=h>>16;h*=UINT32_C(0x7feb352d);
    h^=h>>15;h*=UINT32_C(0x846ca68b);return h^(h>>16);
}
static unsigned gradient(int x,int y,int z,uint32_t seed){
    uint32_t h=avalanche(seed^UINT32_C(0x9e3779b9));
    h=avalanche(h^(uint32_t)x*UINT32_C(0x85ebca6b));
    h=avalanche(h^(uint32_t)y*UINT32_C(0xc2b2ae35));
    return avalanche(h^(uint32_t)z*UINT32_C(0x27d4eb2f))%12u;
}
/* Twelve cube-edge directions; 2D uses their xy projections. */
static float dot(unsigned g,float x,float y,float z){
    static const int8_t directions[12][3]={
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
        {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
        {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
    return (directions[g][0]*x+directions[g][1]*y)+directions[g][2]*z;
}
static int floor_int(double x){int i=(int)x;return i-(x<i);}
static int wrap(int i,unsigned period){int r=i%(int)period;return r<0?r+(int)period:r;}
static float fade(float t){return t*t*t*(t*(t*6.0f-15.0f)+10.0f);}
static float blend(float a,float b,float t){return a+t*(b-a);}

static float perlin(const float p[3],unsigned dimensions,uint32_t seed,const unsigned period[3]){
    int cell[3];float f[3],weight[3];
    for(unsigned axis=0;axis<3;axis++){
        cell[axis]=floor_int(p[axis]);f[axis]=p[axis]-(float)cell[axis];weight[axis]=fade(f[axis]);
    }
    float corners[8];unsigned count=1u<<dimensions;
    for(unsigned corner=0;corner<count;corner++){
        unsigned dx=corner&1u,dy=(corner>>1)&1u,dz=corner>>2;
        unsigned g=gradient(wrap(cell[0]+(int)dx,period[0]),wrap(cell[1]+(int)dy,period[1]),
                            dimensions==3?wrap(cell[2]+(int)dz,period[2]):0,seed);
        corners[corner]=dot(g,f[0]-(float)dx,f[1]-(float)dy,f[2]-(float)dz);
    }
    for(unsigned axis=0;axis<dimensions;axis++){
        count>>=1;
        for(unsigned n=0;n<count;n++)corners[n]=blend(corners[2*n],corners[2*n+1],weight[axis]);
    }
    /* Each corner dot is bounded by two; interpolation is a convex blend. */
    return 0.5f*corners[0];
}

static float simplex(const float p[3],unsigned dimensions,uint32_t seed){
    /* Wide skew/unskew avoids cancellation of f32 cell origins at large
     * coordinates. Corner polynomials remain binary32 for embedded targets. */
    const double skew=dimensions==2?0.36602540378443864676:1.0/3.0;
    const double unskew=dimensions==2?0.21132486540518711775:1.0/6.0;
    double sum=(double)p[0]+p[1]+p[2],offset[3];int cell[3]={0};int total=0;
    for(unsigned axis=0;axis<dimensions;axis++){cell[axis]=floor_int((double)p[axis]+sum*skew);total+=cell[axis];}
    for(unsigned axis=0;axis<dimensions;axis++)offset[axis]=(double)p[axis]-cell[axis]+total*unskew;
    unsigned rank[3]={0};
    for(unsigned a=0;a<dimensions;a++)for(unsigned b=a+1;b<dimensions;b++){
        if(offset[a]>=offset[b])rank[a]++;else rank[b]++;
    }
    float result=0.0f;
    for(unsigned corner=0;corner<=dimensions;corner++){
        int vertex[3]={0};float delta[3]={0};float radius=dimensions==2?0.5f:0.6f;
        for(unsigned axis=0;axis<dimensions;axis++){
            int step=rank[axis]>=dimensions-corner;
            vertex[axis]=cell[axis]+step;
            delta[axis]=(float)(offset[axis]-step+corner*unskew);
            radius-=delta[axis]*delta[axis];
        }
        if(radius>0.0f){
            radius*=radius;
            result+=radius*radius*dot(gradient(vertex[0],vertex[1],vertex[2],seed),delta[0],delta[1],delta[2]);
        }
    }
    return result*(dimensions==2?70.0f:32.0f);
}

bool lvm_noise_eval(unsigned binding,const uint32_t args[7],uint32_t *out){
    if(!lvm_noise_signature(binding).argc)return false;
    unsigned dimensions=(binding==LVM_PERLIN2||binding==LVM_SIMPLEX2)?2:3;
    float p[3]={0};
    for(unsigned axis=0;axis<dimensions;axis++){
        /* Includes NaNs/infinities; flush subnormal coordinates explicitly so
         * soft-float and hardware with flush-to-zero sample the same origin. */
        if((args[axis]&UINT32_C(0x7fffffff))>UINT32_C(0x47800000))return false;
        uint32_t bits=(args[axis]&UINT32_C(0x7f800000))?args[axis]:0;
        memcpy(&p[axis],&bits,4);
    }
    float result;
    if(binding==LVM_PERLIN2||binding==LVM_PERLIN3){
        unsigned period[3]={1,1,1};
        for(unsigned axis=0;axis<dimensions;axis++){
            period[axis]=args[dimensions+1+axis];
            if(!period[axis]||period[axis]>65536)return false;
        }
        result=perlin(p,dimensions,args[dimensions],period);
    }else result=simplex(p,dimensions,args[dimensions]);
    if(result>1.0f)result=1.0f;else if(result< -1.0f)result=-1.0f;
    uint32_t bits;memcpy(&bits,&result,4);
    if(!(bits&UINT32_C(0x7f800000)))bits=0; /* Canonical zero and underflow. */
    *out=bits;return true;
}
