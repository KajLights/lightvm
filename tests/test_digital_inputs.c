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
int main(void) {
    DigitalInputs s={0};DigitalInputConfig config={.id="keys",.sda=4,.scl=5,.address=32,.active_low=true,.debounce_ms=20,.home_assistant=true};
    digital_inputs_apply(&s,&config,1);
    // Held at boot establishes a baseline, including simultaneous keys.
    raw=0;digital_inputs_tick(&s,0);CHECK(s.devices[0].stable==65535&&!s.queued);
    // All releases survive independently of animation frame cadence.
    raw=65535;digital_inputs_tick(&s,5000);digital_inputs_tick(&s,25000);CHECK(s.queued==16);
    DigitalInputEvent e;
    for(unsigned n=0;n<16;n++){CHECK(digital_inputs_event(&s,&e));CHECK(e.channel==n&&!e.pressed&&e.time==25000);}
    // Bouncing press: only the final 20 ms stable interval generates one event.
    raw=65534;digital_inputs_tick(&s,30000);raw=65535;digital_inputs_tick(&s,35000);raw=65534;digital_inputs_tick(&s,40000);
    digital_inputs_tick(&s,55000);CHECK(!s.queued);digital_inputs_tick(&s,60000);CHECK(digital_inputs_event(&s,&e)&&e.channel==0&&e.pressed&&!s.queued);
    // Metadata-only apply retains the baseline and bus ownership.
    digital_inputs_apply(&s,&config,1);digital_inputs_tick(&s,65000);CHECK(!s.queued&&opened==1&&closed==0);
    connected=false;digital_inputs_tick(&s,70000);CHECK(!s.devices[0].available);
    connected=true;raw=0;digital_inputs_tick(&s,1070000);CHECK(s.devices[0].available&&!s.queued);
    // A fixed queue preserves order and reports overflow, never overwrites history.
    for(unsigned n=0;n<6;n++){raw=(n&1)?0:65535;digital_inputs_tick(&s,1100000+n*30000);digital_inputs_tick(&s,1120000+n*30000);}
    CHECK(s.queued==64&&s.overflow==32);digital_inputs_discard_events(&s);CHECK(!s.queued);
    digital_inputs_destroy(&s);CHECK(opened==closed);
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
