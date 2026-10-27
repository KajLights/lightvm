/* Real wire decoder and ingress; deterministic clocks, no sockets or hardware. */
static AudioChallenge gateway_claim_fixture(AudioSources *m,uint64_t owner,const char *name,unsigned profile,bool synthetic){
    char text[256];Json j;AudioChallenge c;
    snprintf(text,sizeof(text),"{\"op\":\"audio_claim\",\"name\":\"%s\",\"source_id\":\"9000000000000007\",\"session\":\"f000000000000008\",\"analysis_profile\":%u,\"synthetic\":%s}",name,profile,synthetic?"true":"false");
    CHECK(json_parse(&j,text,strlen(text)));CHECK(!audio_gateway_claim(m,&j,owner,clock_now,&c));return c;
}
static const char *gateway_fragment_fixture(AudioSources *m,uint64_t owner,uint32_t generation,uint64_t id,const uint8_t *frame,unsigned total,unsigned offset,unsigned length){
    uint8_t packet[FRAME_LIMIT];CHECK(length+AUDIO_FRAGMENT_HEADER<=sizeof(packet));
    write_u32(packet,generation);write_u32(packet+4,(uint32_t)id);write_u32(packet+8,(uint32_t)(id>>32));
    packet[12]=(uint8_t)total;packet[13]=(uint8_t)(total>>8);packet[14]=(uint8_t)offset;packet[15]=(uint8_t)(offset>>8);
    memcpy(packet+AUDIO_FRAGMENT_HEADER,frame+offset,length);
    return audio_gateway_fragment(m,owner,packet,AUDIO_FRAGMENT_HEADER+length,clock_now);
}
static const char *gateway_frame_fixture(AudioSources *m,uint64_t owner,uint32_t generation,uint64_t id,KfFrame *f){
    uint8_t bytes[KF_WIRE_MAX];unsigned size=(unsigned)kf_encode(f,bytes,sizeof(bytes));CHECK(size);
    unsigned first=size>496?496:size;
    const char *error=gateway_fragment_fixture(m,owner,generation,id,bytes,size,0,first);
    return error?error:first==size?NULL:gateway_fragment_fixture(m,owner,generation,id,bytes,size,first,size-first);
}
static void audio_gateway_contract(void){
    AudioSources m={0};clock_now=1000000;clock_step=0;
    InputSourceScope *s=scope_fixture(&m,"{\"music\":{\"type\":\"gateway\",\"name\":\"desk\"}}");
    CHECK(!input_scope_demand(s,true));AudioProvider *p=s->declarations[0].provider;
    AudioChallenge claim=gateway_claim_fixture(&m,11,"desk",1,false);AudioGateway *g=p->gateway;
    CHECK(g&&provider_count(&m)==1&&claim.generation==1);
    clock_now+=2000;
    CHECK(!audio_gateway_clock(&m,11,claim.generation,claim.challenge,9001000,clock_now));
    CHECK(audio_gateway_clock(&m,11,claim.generation,claim.challenge,9001000,clock_now)); /* replay */
    CHECK(audio_gateway_keep(&m,12,claim.generation,clock_now,&claim)); /* wrong owner */
    KfFrame f={.sensor=0x9000000000000007ull,.session=0xf000000000000008ull,.lease=1,.sequence=1,
        .audio_us=9050000,.send_us=9051000,.band_count=64,.event_count=4};
    for(unsigned i=0;i<=64;i++)f.band_edges[i]=(uint16_t)(2+i*510/64);
    for(unsigned i=0;i<64;i++)f.spectrum[i]=(float)i/64;
    for(unsigned i=0;i<6;i++)f.values[i]=(float)i/6;
    for(unsigned i=0;i<4;i++)f.events[i]=(KfEvent){.id=0xf100000000000001ull+i,.time_us=9020000+i*1000,.kind=1,.strength=.5f};
    uint8_t bytes[KF_WIRE_MAX];CHECK(kf_encode(&f,bytes,sizeof(bytes))==700);
    clock_now=1052000;
    CHECK(!gateway_fragment_fixture(&m,11,claim.generation,1,bytes,700,0,496));CHECK(!g->receiver.has_latest&&g->receiver.events==0);
    InputSignal value;uint64_t events;CHECK(input_scope_lookup(s,"music.band63",&value,&events,clock_now)&&!value.valid);
    CHECK(!gateway_fragment_fixture(&m,11,claim.generation,1,bytes,700,496,204));
    CHECK(g->receiver.events==4&&g->receiver.latest.band_count==64);
    CHECK(input_scope_lookup(s,"music.band63",&value,&events,clock_now)&&value.valid&&value.value.f==63.f/64&&events==4);
    CHECK(gateway_fragment_fixture(&m,11,claim.generation,1,bytes,700,0,496));
    CHECK(!gateway_frame_fixture(&m,11,claim.generation,2,&f)&&g->receiver.events==4); /* event repeats */
    /* A reordered record can contribute a previously lost event, never metrics. */
    KfFrame newer=f;newer.sequence=2;newer.audio_us+=20000;newer.send_us+=20000;newer.event_count=0;
    clock_now+=20000;CHECK(!gateway_frame_fixture(&m,11,claim.generation,3,&newer));uint64_t received=g->receiver.received;
    f.events[3].id++;CHECK(!gateway_frame_fixture(&m,11,claim.generation,4,&f)&&g->receiver.events==5&&g->receiver.received==received);
    /* Exact reassembly order, overlap, declared length and 250 ms expiry. */
    CHECK(!gateway_fragment_fixture(&m,11,claim.generation,5,bytes,700,0,400));
    CHECK(gateway_fragment_fixture(&m,11,claim.generation,5,bytes,700,399,201)&&!g->staged);
    CHECK(!gateway_fragment_fixture(&m,11,claim.generation,6,bytes,700,0,496));
    CHECK(gateway_fragment_fixture(&m,11,claim.generation,7,bytes,700,0,496)&&!g->staged);
    CHECK(!gateway_fragment_fixture(&m,11,claim.generation,8,bytes,700,0,496));
    clock_now+=250000;CHECK(gateway_fragment_fixture(&m,11,claim.generation,8,bytes,700,496,204)&&!g->staged);
    CHECK(!gateway_fragment_fixture(&m,11,claim.generation,9,bytes,699,0,496));
    CHECK(gateway_fragment_fixture(&m,11,claim.generation,9,bytes,699,496,203));
    clock_now+=150000;CHECK(!kf_subscriber_available(&g->receiver,clock_now));
    CHECK(gateway_frame_fixture(&m,11,claim.generation,10,&f)); /* receipt cannot refresh old capture */
    /* Distinct canonical policies cost separate slots but observe one claim. */
    InputSourceScope *other=scope_fixture(&m,"{\"audio\":{\"type\":\"gateway\",\"name\":\"desk\",\"analysis_profile\":2}}");
    CHECK(!input_scope_demand(other,true)&&provider_count(&m)==2&&other->declarations[0].provider!=p&&other->declarations[0].provider->gateway==g);
    CHECK(input_scope_lookup(other,"audio.level",&value,&events,clock_now)&&!value.valid);
    uint32_t old_generation=claim.generation;claim=gateway_claim_fixture(&m,22,"desk",2,false);
    CHECK(claim.generation!=old_generation&&!g->receiver.has_latest&&g->receiver.events==5);
    CHECK(audio_gateway_clock(&m,11,old_generation,claim.challenge,9500000,clock_now));
    CHECK(gateway_frame_fixture(&m,11,old_generation,11,&f));
    CHECK(!audio_gateway_clock(&m,22,claim.generation,claim.challenge,9500000,clock_now));
    f.sequence=1;f.audio_us=9500001;f.send_us=9500001;f.event_count=0;clock_now+=10;
    CHECK(!gateway_frame_fixture(&m,22,claim.generation,1,&f));
    CHECK(input_scope_lookup(s,"music.level",&value,&events,clock_now)&&!value.valid);
    CHECK(input_scope_lookup(other,"audio.level",&value,&events,clock_now)&&value.valid);
    /* Keepalive cannot extend clock mapping past five seconds. */
    clock_now+=4000000;CHECK(!audio_gateway_keep(&m,22,claim.generation,clock_now,&claim));
    clock_now+=1100000;f.sequence++;f.audio_us+=5100000;f.send_us=f.audio_us;
    CHECK(gateway_frame_fixture(&m,22,claim.generation,2,&f));
    CHECK(!audio_gateway_clock(&m,22,claim.generation,claim.challenge,14600000,clock_now));
    /* A backward/step clock invalidates data until a new stream is claimed. */
    clock_now+=4000000;CHECK(!audio_gateway_keep(&m,22,claim.generation,clock_now,&claim));
    CHECK(audio_gateway_clock(&m,22,claim.generation,claim.challenge,10,clock_now));CHECK(!g->receiver.client);
    claim=gateway_claim_fixture(&m,33,"desk",1,true);
    CHECK(!audio_gateway_clock(&m,33,claim.generation,claim.challenge,20000000,clock_now));
    f.flags=1;f.sequence=1;f.audio_us=f.send_us=20000001;clock_now+=10;
    CHECK(!gateway_frame_fixture(&m,33,claim.generation,1,&f));
    CHECK(input_scope_lookup(s,"music.level",&value,&events,clock_now)&&!value.valid); /* synthetic policy */
    input_scope_release(other);input_scope_release(s);CHECK(provider_count(&m)==1); /* capture lifetime independent */
    size_t charged=vm_heap_used();CHECK(charged>=sizeof(AudioProvider)+sizeof(AudioGateway));
    InputSourceScope *observer=scope_fixture(&m,"{\"music\":{\"type\":\"gateway\",\"name\":\"desk\",\"allow_synthetic\":true}}");
    CHECK(!input_scope_demand(observer,true)&&provider_count(&m)==1); /* reuse unobserved anchor */
    CHECK(input_scope_lookup(observer,"music.level",&value,&events,clock_now)&&value.valid);
    audio_gateway_disconnect(&m,33);CHECK(!kf_subscriber_available(&observer->declarations[0].provider->gateway->receiver,clock_now));
    audio_sources_tick(&m,clock_now);input_scope_release(observer);CHECK(!provider_count(&m)&&!vm_heap_used());
    claim=gateway_claim_fixture(&m,44,"unobserved",1,false);clock_now+=15000000;audio_sources_tick(&m,clock_now);
    CHECK(!provider_count(&m)&&!vm_heap_used());
    /* Gateway admission shares the four-provider budget with UDP. */
    s=scope_fixture(&m,"{\"a\":{\"type\":\"lvaf\",\"address\":\"10.1.1.1\"},\"b\":{\"type\":\"lvaf\",\"address\":\"10.1.1.2\"},\"c\":{\"type\":\"lvaf\",\"address\":\"10.1.1.3\"},\"d\":{\"type\":\"lvaf\",\"address\":\"10.1.1.4\"}}");
    CHECK(!input_scope_demand(s,true));charged=vm_heap_used();Json j;
    const char *request="{\"op\":\"audio_claim\",\"name\":\"full\",\"source_id\":\"0000000000000001\",\"session\":\"0000000000000002\",\"analysis_profile\":1}";
    CHECK(json_parse(&j,request,strlen(request)));CHECK(audio_gateway_claim(&m,&j,55,clock_now,&claim));
    CHECK(provider_count(&m)==4&&vm_heap_used()==charged);input_scope_release(s);CHECK(!vm_heap_used());
    /* Failed admission must preserve an unobserved claim's canonical policy. */
    claim=gateway_claim_fixture(&m,66,"desk",1,false);
    AudioProvider *anchor=NULL;for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++)if(m.providers[i])anchor=m.providers[i];
    CHECK(anchor);AudioSourceConfig original=anchor->config;
    s=scope_fixture(&m,"{\"a\":{\"type\":\"lvaf\",\"address\":\"10.1.1.1\"},\"b\":{\"type\":\"lvaf\",\"address\":\"10.1.1.2\"},\"c\":{\"type\":\"lvaf\",\"address\":\"10.1.1.3\"}}");
    CHECK(!input_scope_demand(s,true));
    other=scope_fixture(&m,"{\"x\":{\"type\":\"gateway\",\"name\":\"desk\",\"analysis_profile\":2},\"y\":{\"type\":\"gateway\",\"name\":\"desk\",\"analysis_profile\":2},\"z\":{\"type\":\"lvaf\",\"address\":\"10.1.1.4\"}}");
    charged=vm_heap_used();CHECK(input_scope_demand(other,true));
    CHECK(!other->demand&&!anchor->references&&!memcmp(&anchor->config,&original,sizeof(original))&&vm_heap_used()==charged&&provider_count(&m)==4);
    input_scope_release(s);
    AudioSourcePlan plan={0};CHECK(!audio_source_plan_add(&plan,other)&&!audio_source_plan_acquire(&plan));
    CHECK(!memcmp(&anchor->config,&original,sizeof(original)));audio_source_plan_release(&plan);
    CHECK(!memcmp(&anchor->config,&original,sizeof(original))&&!anchor->references&&provider_count(&m)==1);
    CHECK(!input_scope_demand(other,true));
    CHECK(other->declarations[0].provider==anchor&&other->declarations[1].provider==anchor&&anchor->config.profile==2&&anchor->references==2);
    input_scope_release(other);CHECK(!audio_gateway_release(&m,66,claim.generation)&&!vm_heap_used());
}
