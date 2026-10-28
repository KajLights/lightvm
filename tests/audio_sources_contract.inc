/* Deterministic peers; no physical device or socket is involved here. */
static InputSourceScope *scope_fixture(AudioSources *m,const char *document){
    Json j;InputSourceScope *s=NULL;CHECK(json_parse(&j,document,strlen(document)));
    CHECK(!input_scope_parse(m,&j,0,&s));return s;
}
static unsigned provider_count(AudioSources *m){unsigned count=0;for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++)count+=m->providers[i]!=NULL;return count;}
static void fresh_source(AudioProvider *p,float level,uint64_t events){
    KfSubscriber *r=&p->receiver;r->client=1;r->source=7;r->session=8;r->expires=clock_now+15000000;r->received=clock_now;
    r->clock=(KfClockBound){clock_now-1000,clock_now,clock_now};r->has_latest=true;r->events=events;
    r->latest.audio_us=clock_now;r->latest.values[0]=level;
}
static void audio_sources_contract(void){
    clock_now=1000000;clock_step=0;AudioSources manager={0};InputRegistry registry={0};Json j;
    InputSourceScope *s=scope_fixture(&manager,"{\"audio\":{\"type\":\"lvaf\",\"address\":\"010.077.010.245\"}}");
    CHECK(!strcmp(s->declarations[0].config.address,"10.77.10.245")&&!provider_count(&manager));
    CHECK(!input_scope_demand(s,true));CHECK(provider_count(&manager)==1);
    AudioProvider *p=s->declarations[0].provider;fresh_source(p,.2f,9);
    InputPort ports[3]={{.type=T_FLOAT,.initial={.f=.1f}},{.type=T_INT},{.type=T_INT}};
    Portable one={.inputs=ports,.ninputs=2,.input_registry=&registry},two={.inputs=&ports[2],.ninputs=1,.input_registry=&registry};
    inputs_scope(&one,s);inputs_scope(&two,s);CHECK(!inputs_demand(&one,true)&&!inputs_demand(&two,true));
    const char *level_binding="{\"source\":\"audio.level\"}";CHECK(json_parse(&j,level_binding,strlen(level_binding)));CHECK(!input_port_bind(&registry,&ports[0],&j,0,0));
    const char *binding="{\"source\":\"audio.onsets\"}";CHECK(json_parse(&j,binding,strlen(binding)));
    CHECK(!input_port_bind(&registry,&ports[1],&j,0,0)&&!input_port_bind(&registry,&ports[2],&j,0,0));
    input_port_capture(&registry,&ports[1],0);input_port_capture(&registry,&ports[2],0);
    CHECK(ports[1].snapshot.i==0&&ports[2].snapshot.i==0);p->receiver.events++;
    input_port_capture(&registry,&ports[1],0);input_port_capture(&registry,&ports[2],0);
    CHECK(ports[1].snapshot.i==1&&ports[2].snapshot.i==1);
    CHECK(!inputs_demand(&two,false));p->receiver.events+=3;CHECK(!inputs_demand(&two,true));p->receiver.events++;
    input_port_capture(&registry,&ports[1],0);input_port_capture(&registry,&ports[2],0);
    CHECK(ports[1].snapshot.i==5&&ports[2].snapshot.i==2);
    input_port_capture(&registry,&ports[0],0);CHECK(ports[0].snapshot.f==.2f);
    const char *names[]={"audio.level"};unsigned types[]={T_FLOAT};uint32_t generation;
    CHECK(!input_registry_claim_typed(&registry,names,types,1,&generation));InputValue value={.f=.9f};
    CHECK(!input_registry_publish_typed(&registry,generation,1,names,&value,1,clock_now));
    clock_now+=400000;input_port_capture(&registry,&ports[0],0);
    CHECK(ports[0].fallback&&ports[0].snapshot.f==.1f); /* No global fallthrough or 1 s grace. */
    input_port_capture(&registry,&ports[1],0);CHECK(ports[1].snapshot.i==5); /* A gap cannot reset/replay the occurrence count. */
    ports[0].driver.hold=true;input_port_capture(&registry,&ports[0],0);CHECK(!ports[0].fallback&&ports[0].snapshot.f==.2f);
    InputSourceScope *shared=scope_fixture(&manager,"{\"room\":{\"type\":\"lvaf\",\"address\":\"10.77.10.245\"}}");
    CHECK(!input_scope_demand(shared,true)&&shared->declarations[0].provider==p&&provider_count(&manager)==1);
    InputSourceScope *different=scope_fixture(&manager,"{\"room\":{\"type\":\"lvaf\",\"address\":\"10.77.10.245\",\"allow_synthetic\":true}}");
    CHECK(!input_scope_demand(different,true)&&different->declarations[0].provider!=p);
    InputSourceScope *gateway=scope_fixture(&manager,"{\"desk\":{\"type\":\"gateway\",\"name\":\"music\"}}");
    CHECK(!input_scope_demand(gateway,true)&&provider_count(&manager)==3);
    InputSourceScope *too_many=scope_fixture(&manager,"{\"a\":{\"type\":\"lvaf\",\"address\":\"10.1.1.1\"},\"b\":{\"type\":\"lvaf\",\"address\":\"10.1.1.2\"}}");
    size_t heap=vm_heap_used();CHECK(input_scope_demand(too_many,true));CHECK(provider_count(&manager)==3&&vm_heap_used()==heap&&!too_many->demand);
    input_scope_release(too_many);input_scope_release(gateway);input_scope_release(different);input_scope_release(shared);
    inputs_scope(&one,NULL);inputs_scope(&two,NULL);input_scope_release(s);input_registry_destroy(&registry);
    CHECK(!provider_count(&manager)&&vm_heap_used()==0);
    const char *bad[]={"{\"board\":{\"type\":\"lvaf\",\"address\":\"10.1.1.1\"}}",
        "{\"a\":{\"type\":\"lvaf\",\"address\":\"sense.local\"}}",
        "{\"a\":{\"type\":\"lvaf\",\"address\":\"239.1.1.1\"}}",
        "{\"a\":{\"type\":\"lvaf\",\"address\":\"0.0.0.0\"}}",
        "{\"a\":{\"type\":\"gateway\",\"name\":\"music\",\"analysis_profile\":0}}"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
        CHECK(json_parse(&j,bad[i],strlen(bad[i])));CHECK(input_scope_parse(&manager,&j,0,&s)&&!s&&vm_heap_used()==0);
    }
}
