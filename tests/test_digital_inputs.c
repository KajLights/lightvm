#include "digital_inputs.h"
#include "apa102.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static uint16_t raw=65535;
static bool connected=true;
static unsigned opened,closed;
bool platform_i2c_open(int32_t sda,int32_t scl,void **p) { CHECK(sda==4&&scl==5);*p=&raw;opened++;return true; }
void platform_i2c_close(void *p) { CHECK(p==&raw);closed++; }
bool platform_i2c_transfer(void *p,uint8_t address,const uint8_t *w,size_t wn,uint8_t *r,size_t rn) {
    CHECK(p==&raw&&address==32);
    if(!connected)return false;
    if(rn){CHECK(wn==1&&w[0]==0&&rn==2);r[0]=(uint8_t)raw;r[1]=(uint8_t)(raw>>8);}
    else CHECK(wn==3&&((w[0]==4&&w[1]==0&&w[2]==0)||(w[0]==6&&w[1]==255&&w[2]==255)));
    return true;
}
static DigitalInputs inputs;
static uint64_t now;
static DigitalInputConfig config={.id="keys",.sda=4,.scl=5,.address=32,.active_low=true,
    .debounce_ms=20,.long_press_ms=500,.multi_press_ms=300,.home_assistant=true};
static void boot(uint16_t held) {
    digital_inputs_destroy(&inputs);connected=true;now=0;raw=(uint16_t)~held;
    digital_inputs_apply(&inputs,&config,1);digital_inputs_tick(&inputs,now);
    CHECK(inputs.devices[0].available&&!inputs.queued);
}
static void step(uint64_t elapsed) { now+=elapsed;digital_inputs_tick(&inputs,now); }
static void edge(uint16_t held) { raw=(uint16_t)~held;step(5000);step(20000); }
static DigitalInputEvent expect(DigitalInputEventType type,unsigned channel,unsigned count) {
    DigitalInputEvent event;CHECK(digital_inputs_event(&inputs,&event));
    CHECK(event.type==type&&event.channel==channel&&event.multi_press_count==count);
    return event;
}
static void drain(void) { DigitalInputEvent event;while(digital_inputs_event(&inputs,&event)){} }
static void gestures(void) {
    // Boot/recovery baselines never synthesize a gesture, including a later release.
    boot(65535);step(1000000);edge(0);CHECK(!inputs.queued);
    edge(1);expect(DIGITAL_PRESS_START,0,0);
    edge(0);expect(DIGITAL_PRESS_END,0,0);uint64_t up=now;
    step(295000);CHECK(!inputs.queued);step(5000);
    CHECK(expect(DIGITAL_MULTI_PRESS_END,0,1).time==up+300000);
    // Two complete short taps, including immediate edges and intermediate count.
    edge(1);expect(DIGITAL_PRESS_START,0,0);edge(0);expect(DIGITAL_PRESS_END,0,0);
    edge(1);expect(DIGITAL_PRESS_START,0,0);expect(DIGITAL_MULTI_PRESS_ONGOING,0,2);
    // Matter: a hold inside an existing multi-press cycle stays in that cycle.
    step(600000);CHECK(!inputs.queued);edge(0);expect(DIGITAL_PRESS_END,0,0);
    step(300000);expect(DIGITAL_MULTI_PRESS_END,0,2);CHECK(!inputs.queued);
    // A standalone hold emits its start exactly once, including after a service stall.
    edge(1);expect(DIGITAL_PRESS_START,0,0);uint64_t down=now;step(600000);
    CHECK(expect(DIGITAL_LONG_PRESS_START,0,0).time==down+500000);
    step(1000000);CHECK(!inputs.queued);edge(0);expect(DIGITAL_LONG_PRESS_END,0,0);
    step(300000);CHECK(!inputs.queued);
    // An idle gap exactly at the deadline completes the previous single tap first.
    edge(1);expect(DIGITAL_PRESS_START,0,0);edge(0);expect(DIGITAL_PRESS_END,0,0);
    raw=65534;step(280000);step(20000);
    expect(DIGITAL_MULTI_PRESS_END,0,1);expect(DIGITAL_PRESS_START,0,0);CHECK(!inputs.queued);
    // Independent per-key hold clocks, even with simultaneous keys.
    boot(0);edge(1);expect(DIGITAL_PRESS_START,0,0);step(100000);
    edge(3);expect(DIGITAL_PRESS_START,1,0);step(375000);expect(DIGITAL_LONG_PRESS_START,0,0);
    step(125000);expect(DIGITAL_LONG_PRESS_START,1,0);edge(0);
    expect(DIGITAL_LONG_PRESS_END,0,0);expect(DIGITAL_LONG_PRESS_END,1,0);CHECK(!inputs.queued);
    // Mechanical bounce produces only one debounced press.
    boot(0);raw=65534;step(5000);raw=65535;step(5000);raw=65534;step(5000);
    step(15000);CHECK(!inputs.queued);step(5000);expect(DIGITAL_PRESS_START,0,0);CHECK(!inputs.queued);
    // Metadata/configuration changes retain bus ownership but cancel incomplete gestures.
    unsigned before=opened;digital_inputs_apply(&inputs,&config,1);step(600000);CHECK(!inputs.queued&&opened==before);
    edge(0);CHECK(!inputs.queued);
    // An outage discards queued and unfinished gestures; reconnect while held is silent.
    edge(1);connected=false;step(5000);CHECK(!inputs.devices[0].available&&!inputs.queued);
    connected=true;step(1000000);step(600000);edge(0);CHECK(!inputs.queued);
    // HA/MQTT session reset cancels both a pending single and an in-progress hold.
    edge(1);drain();edge(0);drain();digital_inputs_discard_events(&inputs);step(300000);CHECK(!inputs.queued);
    edge(1);drain();digital_inputs_discard_events(&inputs);step(600000);edge(0);CHECK(!inputs.queued);
    // Configurable thresholds use accepted edges, not an earlier raw transition.
    config.long_press_ms=150;config.multi_press_ms=100;boot(0);
    edge(1);expect(DIGITAL_PRESS_START,0,0);step(145000);CHECK(!inputs.queued);
    step(5000);expect(DIGITAL_LONG_PRESS_START,0,0);edge(0);expect(DIGITAL_LONG_PRESS_END,0,0);
    edge(1);expect(DIGITAL_PRESS_START,0,0);edge(0);expect(DIGITAL_PRESS_END,0,0);
    step(95000);CHECK(!inputs.queued);step(5000);expect(DIGITAL_MULTI_PRESS_END,0,1);
    config.long_press_ms=500;config.multi_press_ms=300;
    // Triple press and a bounded count: overflow aborts rather than wrapping to single.
    boot(0);
    for(unsigned n=1;n<=3;n++){
        edge(1);expect(DIGITAL_PRESS_START,0,0);if(n>1)expect(DIGITAL_MULTI_PRESS_ONGOING,0,n);
        edge(0);expect(DIGITAL_PRESS_END,0,0);
    }
    step(300000);expect(DIGITAL_MULTI_PRESS_END,0,3);
    for(unsigned n=1;n<=DIGITAL_INPUT_MULTI_PRESS_MAX+2;n++){
        edge(1);
        if(n<=DIGITAL_INPUT_MULTI_PRESS_MAX){expect(DIGITAL_PRESS_START,0,0);if(n>1)expect(DIGITAL_MULTI_PRESS_ONGOING,0,n);}
        else CHECK(!inputs.queued);
        edge(0);expect(DIGITAL_PRESS_END,0,0);
    }
    step(300000);expect(DIGITAL_MULTI_PRESS_END,0,0);CHECK(!inputs.queued);
    edge(1);expect(DIGITAL_PRESS_START,0,0);
    // Fixed queue preserves order and reports loss without overwriting history.
    boot(0);uint32_t overflow=inputs.overflow;
    for(unsigned n=0;n<6;n++)edge(n&1?0:65535);
    CHECK(inputs.queued==64&&inputs.overflow==overflow+64);
    for(unsigned n=0;n<16;n++)expect(DIGITAL_PRESS_START,n,0);
    for(unsigned n=0;n<16;n++)expect(DIGITAL_PRESS_END,n,0);
    for(unsigned n=0;n<16;n++){expect(DIGITAL_PRESS_START,n,0);expect(DIGITAL_MULTI_PRESS_ONGOING,n,2);}
    CHECK(!inputs.queued);
    digital_inputs_destroy(&inputs);CHECK(opened==closed);
    CHECK(!strcmp(digital_input_event_name(DIGITAL_MULTI_PRESS_END),"multi_press_end"));
    CHECK(!digital_input_event_name((DigitalInputEventType)99));
}
static void configuration_permutations(void){
    DigitalInputConfig configs[3]={config,config,config};
    for(unsigned n=0;n<3;n++)snprintf(configs[n].id,sizeof(configs[n].id),"keys%u",n);
    digital_inputs_apply(&inputs,configs,3);digital_inputs_tick(&inputs,0);
    for(unsigned n=0;n<3;n++)inputs.devices[n].stable=(uint16_t)(100+n);
    unsigned before_open=opened,before_close=closed;
    DigitalInputConfig reordered[3]={configs[2],configs[0],configs[1]};
    digital_inputs_apply(&inputs,reordered,3);
    CHECK(inputs.devices[0].stable==102&&inputs.devices[1].stable==100&&inputs.devices[2].stable==101);
    CHECK(opened==before_open&&closed==before_close);
    DigitalInputConfig replaced[3]={configs[1],configs[2],config};strcpy(replaced[2].id,"new");
    digital_inputs_apply(&inputs,replaced,3);
    CHECK(inputs.devices[0].config==&replaced[0]&&inputs.devices[1].config==&replaced[1]);
    CHECK(inputs.devices[0].stable==101&&inputs.devices[1].stable==102&&!inputs.devices[2].bus);
    CHECK(closed==before_close+1);
    digital_inputs_apply(&inputs,replaced+1,1);CHECK(inputs.devices[0].stable==102);
    CHECK(closed==before_close+2);digital_inputs_destroy(&inputs);CHECK(opened==closed);
}
int main(void) {
    gestures();configuration_permutations();
    // APA102 wire format: start frame, BGR, brightness multiplier, and enough
    // trailing clocks for strips longer than this keypad.
    PixelConfig pixels={.count=33,.brightness=128,.order="BGR"};uint8_t rgb[99],wire[139];memset(rgb,0,sizeof(rgb));
    rgb[0]=255;rgb[1]=127;rgb[2]=31;CHECK(apa102_frame_size(33)==139);
    CHECK(apa102_encode(&pixels,rgb,33,wire,sizeof(wire)));
    CHECK(!memcmp(wire,"\0\0\0\0",4)&&wire[4]==255&&wire[5]==16&&wire[6]==64&&wire[7]==128);
    CHECK(wire[136]==255&&wire[137]==255&&wire[138]==255);
    CHECK(!apa102_encode(&pixels,rgb,33,wire,sizeof(wire)-1));
    return 0;
}
