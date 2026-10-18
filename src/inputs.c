#include "inputs.h"
#include <string.h>

__attribute__((weak)) bool platform_i2c_valid(int32_t sda,int32_t scl) { (void)sda;(void)scl;return false; }
__attribute__((weak)) bool platform_i2c_open(int32_t sda,int32_t scl,void **p) { (void)sda;(void)scl;(void)p;return false; }
__attribute__((weak)) void platform_i2c_close(void *p) { (void)p; }
__attribute__((weak)) bool platform_i2c_transfer(void *p,uint8_t a,const uint8_t *w,size_t wn,uint8_t *r,size_t rn) {
    (void)p;(void)a;(void)w;(void)wn;(void)r;(void)rn;return false;
}
void inputs_discard_events(Inputs *s) { s->head=0;s->queued=0; }
void inputs_destroy(Inputs *s) {
    for(unsigned i=0;i<s->count;i++)if(s->devices[i].bus)platform_i2c_close(s->devices[i].bus);
    memset(s,0,sizeof(*s));
}
void inputs_apply(Inputs *s,const InputConfig *config,unsigned count) {
    // Preserve sampling baselines for unchanged adapters; configuration is bounded.
    DigitalInput next[INPUT_LIMIT]={0};
    for(unsigned n=0;n<count;n++){
        for(unsigned i=0;i<s->count;i++)if(s->devices[i].bus &&
            !strcmp(s->devices[i].config.id,config[n].id)&&s->devices[i].config.sda==config[n].sda&&
            s->devices[i].config.scl==config[n].scl&&s->devices[i].config.address==config[n].address&&
            s->devices[i].config.active_low==config[n].active_low&&s->devices[i].config.debounce_ms==config[n].debounce_ms){
            next[n]=s->devices[i];s->devices[i].bus=NULL;break;
        }
        next[n].config=config[n];
    }
    uint32_t overflow=s->overflow;inputs_destroy(s);s->overflow=overflow;
    memcpy(s->devices,next,sizeof(next));s->count=count;
}
void inputs_tick(Inputs *s,uint64_t now) {
    for(unsigned i=0;i<s->count;i++){
        DigitalInput *d=&s->devices[i];if(now<d->next_sample)continue;
        d->next_sample=now+5000;
        if(!d->bus&&!platform_i2c_open(d->config.sda,d->config.scl,&d->bus))goto unavailable;
        if(!d->configured){
            // TCA9555: no polarity inversion, all sixteen channels are inputs.
            const uint8_t polarity[]={4,0,0},direction[]={6,255,255};
            if(!platform_i2c_transfer(d->bus,(uint8_t)d->config.address,polarity,3,NULL,0)||
               !platform_i2c_transfer(d->bus,(uint8_t)d->config.address,direction,3,NULL,0))goto unavailable;
            d->configured=true;
        }
        uint8_t reg=0,bytes[2];
        if(!platform_i2c_transfer(d->bus,(uint8_t)d->config.address,&reg,1,bytes,2))goto unavailable;
        uint16_t value=(uint16_t)(bytes[0]|(bytes[1]<<8));if(d->config.active_low)value=(uint16_t)~value;
        d->available=true;
        if(!d->initialized){d->stable=value;d->candidate=value;d->initialized=true;for(unsigned n=0;n<16;n++)d->changed[n]=now;continue;}
        for(unsigned n=0;n<16;n++){
            uint16_t bit=(uint16_t)(1u<<n);
            if((value^d->candidate)&bit){d->candidate^=bit;d->changed[n]=now;}
            if(((d->stable^d->candidate)&bit)&&now-d->changed[n]>=d->config.debounce_ms*1000u){
                d->stable^=bit;
                if(!d->config.home_assistant)continue;
                if(s->queued==INPUT_EVENT_LIMIT){s->overflow++;continue;}
                unsigned tail=(s->head+s->queued++)%INPUT_EVENT_LIMIT;
                s->events[tail]=(InputEvent){.time=now,.input=i,.channel=n,.pressed=(value&bit)!=0};
            }
        }
        continue;
unavailable:
        d->available=false;d->initialized=false;d->configured=false;d->next_sample=now+1000000;
    }
}
bool inputs_event(Inputs *s,InputEvent *event) {
    if(!s->queued)return false;
    *event=s->events[s->head];s->head=(s->head+1)%INPUT_EVENT_LIMIT;s->queued--;return true;
}
