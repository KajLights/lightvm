/* Independent LVAF fixture encoding; exercise the actual registry/interpreter. */
#include "audio_receiver.h"
static void audio_put(uint8_t *p,uint64_t value,unsigned n){while(n){p[--n]=(uint8_t)value;value>>=8;}}
static size_t audio_packet(uint8_t *p,uint64_t session,uint64_t seq,uint64_t audio,float level,uint64_t event){
    memset(p,0,AUDIO_PACKET_LIMIT);memcpy(p,"LVAF",4);p[4]=4;p[5]=6;p[6]=event?1:0;
    audio_put(p+8,42,8);audio_put(p+16,session,8);audio_put(p+24,seq,8);
    audio_put(p+32,audio,8);audio_put(p+40,audio+2000,8);
    audio_put(p+48,48000,4);audio_put(p+52,1024,2);audio_put(p+54,2048,2);p[81]=1;
    for(unsigned i=0;i<6;i++){float value=i?0.25f:level;uint32_t bits;memcpy(&bits,&value,4);audio_put(p+56+4*i,bits,4);}
    if(event){audio_put(p+84,event,8);audio_put(p+92,audio,8);p[100]=1;audio_put(p+104,0x3f000000,4);}
    return 84+(event?24:0);
}
static InputSignal *audio_signal(InputRegistry *r,const char *name){
    for(unsigned n=0;n<r->count;n++)if(!strcmp(r->signals[n].name,name))return &r->signals[n];
    CHECK(false);return NULL;
}
static void audio_receiver_contract(void){
    uint8_t packet[AUDIO_PACKET_LIMIT],other[AUDIO_PACKET_LIMIT];AudioFrame decoded;
    size_t size=audio_packet(packet,1,1,1000000,.8f,1);
    CHECK(audio_decode(packet,size,&decoded)&&decoded.events[0].id==1&&decoded.values[0]==.8f);
    for(size_t n=0;n<size;n++)CHECK(!audio_decode(packet,n,&decoded));
    const unsigned offsets[]={0,4,5,6,7,48,52,54,80,81,82,83,100,101,102,103};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){
        memcpy(other,packet,size);other[offsets[i]]=255;CHECK(!audio_decode(other,size,&decoded));
    }
    memcpy(other,packet,size);audio_put(other+56,0x7fc00000,4);CHECK(!audio_decode(other,size,&decoded));
    memcpy(other,packet,size);audio_put(other+40,1350001,8);CHECK(!audio_decode(other,size,&decoded));
    audio_put(other+40,1350000,8);CHECK(audio_decode(other,size,&decoded));
    /* Maximum spectrum layout and four events still fit; bad ranges fail. */
    size=audio_packet(packet,1,1,1000000,.8f,0);packet[80]=64;packet[6]=4;
    for(unsigned i=0;i<64;i++){audio_put(packet+84+i*8,2+i*510/64,2);audio_put(packet+86+i*8,2+(i+1)*510/64,2);}
    for(unsigned i=0;i<4;i++){unsigned at=596+i*24;audio_put(packet+at,i+1,8);audio_put(packet+at+8,900000+i,8);packet[at+16]=1;audio_put(packet+at+20,0x3f000000,4);}
    CHECK(audio_decode(packet,692,&decoded)&&decoded.bands==64&&decoded.event_count==4);
    packet[86]=255;CHECK(!audio_decode(packet,692,&decoded));

    InputRegistry registry={0};AudioReceiver r={.config={.sensor=42}};
    const char *const bass[]={"audio.bass"};const unsigned float_type[]={T_FLOAT};uint32_t old;
    CHECK(!input_registry_claim_typed(&registry,bass,float_type,1,&old));
    CHECK(!audio_receiver_claim(&r,&registry)); /* Existing slots need not be in declaration order. */
    size=audio_packet(packet,1,1,1000000,.2f,1);
    unsigned before=allocations;deny_allocation=true;
    audio_receiver_receive(&r,packet,size,100);
    size=audio_packet(packet,1,2,1106667,.8f,0);
    audio_receiver_receive(&r,packet,size,101);
    CHECK(r.coalesced==1&&r.onsets==1&&r.pending&&!r.published);
    audio_receiver_publish(&r,&registry,102);
    CHECK(r.published==1&&r.max_service_us==1&&!r.pending);
    CHECK(audio_signal(&registry,"audio.level")->value.f==.8f);
    CHECK(audio_signal(&registry,"audio.bass")->value.f==.25f);
    CHECK(audio_signal(&registry,"audio.onsets")->value.i==1);
    CHECK(audio_signal(&registry,"audio.level")->received==101);
    /* Duplicate and reordered packets neither rewind metrics nor renew freshness. */
    size=audio_packet(packet,1,1,1000000,.2f,1);audio_receiver_receive(&r,packet,size,200);
    audio_receiver_publish(&r,&registry,201);
    CHECK(r.duplicates==1&&r.published==1&&audio_signal(&registry,"audio.level")->received==101);
    size=audio_packet(packet,1,1,1000000,.2f,2);audio_receiver_receive(&r,packet,size,202);
    audio_receiver_publish(&r,&registry,203);
    CHECK(r.onsets==2&&audio_signal(&registry,"audio.level")->received==101);
    size=audio_packet(packet,2,1,1300000,.5f,1);audio_receiver_receive(&r,packet,size,300);
    CHECK(r.session==1);audio_receiver_receive(&r,packet,size,1000200);
    CHECK(r.session==2&&r.sequence==1);audio_receiver_publish(&r,&registry,1000201);
    size=audio_packet(packet,1,3,1400000,.1f,0);audio_receiver_receive(&r,packet,size,3000000);CHECK(r.session==2);
    size=audio_packet(packet,2,2,1500000,.1f,0);packet[7]=1;audio_receiver_receive(&r,packet,size,3000000);CHECK(r.sequence==1);
    packet[7]=0;audio_put(packet+8,43,8);audio_receiver_receive(&r,packet,size,3000000);CHECK(r.sequence==1);
    CHECK(allocations==before);deny_allocation=false;

    /* A revoked generation stops the adapter instead of reclaiming ownership. */
    CHECK(!input_registry_claim_typed(&registry,bass,float_type,1,&old));
    size=audio_packet(packet,2,2,1500000,.1f,0);audio_receiver_receive(&r,packet,size,3000001);
    audio_receiver_publish(&r,&registry,3000002);CHECK(r.conflict);
    input_registry_destroy(&registry);CHECK(vm_heap_used()==0);

    /* All sixteen transmitted spectrum values publish atomically with metrics.
     * Missing bands in a new session clear previous spectrum values. */
    r=(AudioReceiver){.config={.sensor=42}};
    CHECK(!audio_receiver_claim(&r,&registry));
    size=audio_packet(packet,3,1,1000000,.5f,0);packet[80]=16;size+=16*8;
    for(unsigned i=0;i<16;i++){
        float value=(i+1)/16.0f;uint32_t bits;memcpy(&bits,&value,4);
        audio_put(packet+84+i*8,2+i*510/16,2);audio_put(packet+86+i*8,2+(i+1)*510/16,2);
        audio_put(packet+88+i*8,bits,4);
    }
    before=allocations;deny_allocation=true;
    audio_receiver_receive(&r,packet,size,100);audio_receiver_publish(&r,&registry,101);
    for(unsigned i=0;i<16;i++){
        char name[33];snprintf(name,sizeof(name),"audio.band%02u",i);
        InputSignal *s=audio_signal(&registry,name);
        CHECK(s->value.f==(i+1)/16.0f&&s->received==100&&s->sequence==1);
    }
    CHECK(audio_signal(&registry,"audio.level")->value.f==.5f);
    size=audio_packet(packet,4,1,1000000,.5f,0);
    audio_receiver_receive(&r,packet,size,1000200);audio_receiver_publish(&r,&registry,1000201);
    CHECK(audio_signal(&registry,"audio.band15")->value.f==0&&r.session==4);
    CHECK(allocations==before);deny_allocation=false;
    input_registry_destroy(&registry);CHECK(vm_heap_used()==0);

#if LIGHTVM_AUDIO_MULTICAST
    const char *config="{\"sensor_id\":\"000000000000002a\"}";Json j;AudioReceiver *allocated=NULL;char error[160];
    CHECK(json_parse(&j,config,strlen(config))&&audio_config_parse(&j,0,&allocated,error,sizeof(error)));
    CHECK(allocated&&allocated->config.sensor==42&&allocated->config.group[0]==239&&allocated->config.port==49152);
    demo_free(allocated);
    const char *bad[]={"{\"sensor_id\":\"0000000000000000\"}","{\"sensor_id\":\"000000000000002a\",\"group\":\"240.1.2.3\"}",
        "{\"sensor_id\":\"000000000000002a\",\"port\":0}","{\"sensor_id\":\"000000000000002a\",\"extra\":true}"};
    for(unsigned i=0;i<4;i++){CHECK(json_parse(&j,bad[i],strlen(bad[i])));CHECK(!audio_config_parse(&j,0,&allocated,error,sizeof(error))&&!allocated);}
#endif
    CHECK(vm_heap_used()==0);
}
