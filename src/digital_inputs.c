#include "digital_inputs.h"
#include <string.h>

__attribute__((weak)) bool platform_i2c_valid(int32_t sda,int32_t scl) { (void)sda;(void)scl;return false; }
__attribute__((weak)) bool platform_i2c_open(int32_t sda,int32_t scl,void **p) { (void)sda;(void)scl;(void)p;return false; }
__attribute__((weak)) void platform_i2c_close(void *p) { (void)p; }
__attribute__((weak)) bool platform_i2c_transfer(void *p,uint8_t a,const uint8_t *w,size_t wn,uint8_t *r,size_t rn) {
    (void)p;(void)a;(void)w;(void)wn;(void)r;(void)rn;return false;
}
const char *digital_input_event_name(DigitalInputEventType type) {
    static const char *const names[]={"press_start","press_end","long_press_start","long_press_end","multi_press_ongoing","multi_press_end"};
    return (unsigned)type<sizeof(names)/sizeof(names[0])?names[type]:NULL;
}
void digital_inputs_discard_events(DigitalInputs *s) {
    s->head=0;s->queued=0;
    for(unsigned i=0;i<s->count;i++)memset(s->devices[i].gestures,0,sizeof(s->devices[i].gestures));
}
static void emit(DigitalInputs *s,unsigned input,unsigned channel,DigitalInputEventType type,unsigned count,uint64_t time) {
    if(s->queued==DIGITAL_INPUT_EVENT_LIMIT){s->overflow++;return;}
    unsigned tail=(s->head+s->queued++)%DIGITAL_INPUT_EVENT_LIMIT;
    s->events[tail]=(DigitalInputEvent){.time=time,.input=input,.channel=channel,.type=type,.multi_press_count=count};
}
static void cancel(DigitalInputs *s,unsigned input) {
    memset(s->devices[input].gestures,0,sizeof(s->devices[input].gestures));
    // Loss of acquisition also invalidates this adapter's unpublished edges.
    unsigned kept=0;
    for(unsigned n=0;n<s->queued;n++){
        DigitalInputEvent e=s->events[(s->head+n)%DIGITAL_INPUT_EVENT_LIMIT];
        if(e.input!=input)s->events[(s->head+kept++)%DIGITAL_INPUT_EVENT_LIMIT]=e;
    }
    s->queued=kept;
}
static void gesture_tick(DigitalInputs *s,unsigned input,unsigned channel,uint64_t now) {
    DigitalInput *d=&s->devices[input];DigitalInputGesture *g=&d->gestures[channel];
    if(!g->count)return;
    if(g->down){
        // Only the first press can become a hold (Matter MS/MSR/MSL/MSM, !AS).
        uint64_t due=g->pressed_at+(uint64_t)d->config.long_press_ms*1000u;
        if(g->count==1&&!g->long_started&&now>=due){
            g->long_started=true;emit(s,input,channel,DIGITAL_LONG_PRESS_START,0,due);
        }
    }else{
        uint64_t due=g->released_at+(uint64_t)d->config.multi_press_ms*1000u;
        if(now>=due){
            emit(s,input,channel,DIGITAL_MULTI_PRESS_END,g->count>DIGITAL_INPUT_MULTI_PRESS_MAX?0:g->count,due);
            memset(g,0,sizeof(*g));
        }
    }
}
static void gesture_edge(DigitalInputs *s,unsigned input,unsigned channel,bool pressed,uint64_t now) {
    DigitalInputGesture *g=&s->devices[input].gestures[channel];
    if(pressed){
        g->down=true;g->pressed_at=now;
        if(g->count<=DIGITAL_INPUT_MULTI_PRESS_MAX)g->count++;
        // Overlong sequences abort with count zero once idle; do not wrap.
        if(g->count>DIGITAL_INPUT_MULTI_PRESS_MAX)return;
        emit(s,input,channel,DIGITAL_PRESS_START,0,now);
        if(g->count>1)emit(s,input,channel,DIGITAL_MULTI_PRESS_ONGOING,g->count,now);
    }else if(g->down){
        g->down=false;
        if(g->long_started){
            emit(s,input,channel,DIGITAL_LONG_PRESS_END,0,now);memset(g,0,sizeof(*g));
        }else{
            emit(s,input,channel,DIGITAL_PRESS_END,0,now);g->released_at=now;
        }
    }
}
void digital_inputs_destroy(DigitalInputs *s) {
    for(unsigned i=0;i<s->count;i++)if(s->devices[i].bus)platform_i2c_close(s->devices[i].bus);
    memset(s,0,sizeof(*s));
}
void digital_inputs_apply(DigitalInputs *s,const DigitalInputConfig *config,unsigned count) {
    // Preserve sampling baselines for unchanged adapters; configuration is bounded.
    DigitalInput next[DIGITAL_INPUT_LIMIT]={0};
    for(unsigned n=0;n<count;n++){
        for(unsigned i=0;i<s->count;i++)if(s->devices[i].bus &&
            !strcmp(s->devices[i].config.id,config[n].id)&&s->devices[i].config.sda==config[n].sda&&
            s->devices[i].config.scl==config[n].scl&&s->devices[i].config.address==config[n].address&&
            s->devices[i].config.active_low==config[n].active_low&&s->devices[i].config.debounce_ms==config[n].debounce_ms){
            next[n]=s->devices[i];s->devices[i].bus=NULL;break;
        }
        next[n].config=config[n];
    }
    uint32_t overflow=s->overflow;digital_inputs_destroy(s);s->overflow=overflow;
    memcpy(s->devices,next,sizeof(next));s->count=count;
    digital_inputs_discard_events(s);
}
void digital_inputs_tick(DigitalInputs *s,uint64_t now) {
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
            if(d->config.home_assistant)gesture_tick(s,i,n,now);
            if((value^d->candidate)&bit){d->candidate^=bit;d->changed[n]=now;}
            if(((d->stable^d->candidate)&bit)&&now-d->changed[n]>=d->config.debounce_ms*1000u){
                d->stable^=bit;
                if(d->config.home_assistant)gesture_edge(s,i,n,(d->stable&bit)!=0,now);
            }
        }
        continue;
unavailable:
        cancel(s,i);
        d->available=false;d->initialized=false;d->configured=false;d->next_sample=now+1000000;
    }
}
bool digital_inputs_event(DigitalInputs *s,DigitalInputEvent *event) {
    if(!s->queued)return false;
    *event=s->events[s->head];s->head=(s->head+1)%DIGITAL_INPUT_EVENT_LIMIT;s->queued--;return true;
}
