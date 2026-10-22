/* Resumability, transport independence and cancellation of real engine work. */
static void preparation_contract(void){
    clock_now=0;clock_step=0;storage_available=true;deny_allocation=false;deny_show=false;
    memset(flash,255,sizeof(flash));static App app;Capture reply={.marker=0x12345678u};
    app_init(&app,capture,&reply);Session session;session_init(&session,&app,1,capture,&reply);
    const char *config="{\"schema\":\"lightvm.config\",\"outputs\":[{\"id\":\"lights\",\"type\":\"ws2812b\",\"config\":{\"pin\":\"GP16\",\"count\":17}}]}";
    memcpy(app.staging,config,strlen(config));app.expected=(uint32_t)strlen(config);app.staged=true;app.object_kind=1;app.upload_owner=1;
    const char *apply="{\"op\":\"config_apply\",\"expected_revision\":0}";
    command(&session,CMD_MANAGE,(const uint8_t *)apply,strlen(apply));CHECK(reply.code==STATUS_OK);
    CHECK(store_put(&app.store,STORE_ANIMATION,"slow",slow_lbc,sizeof(slow_lbc)));
    CHECK(store_put(&app.store,STORE_ANIMATION,"counter",cycling_lbc,sizeof(cycling_lbc)));
    const char *activate="{\"op\":\"program_activate\",\"name\":\"counter\",\"output\":\"lights\"}";
    command(&session,CMD_MANAGE,(const uint8_t *)activate,strlen(activate));CHECK(reply.code==STATUS_OK);
    AppOutput *d=app.outputs[0];uint32_t old_generation=d->vm.generation;
    size_t baseline=vm_heap_used();
    PreparationRequest request={.kind=PREPARE_DIRECT,.play=true,.revision=d->composition_revision,.light=d->pixels.light,.entry=*store_find(&app.store,STORE_ANIMATION,"slow")};
    strcpy(request.name,"slow");
    unsigned total=0;
    for(unsigned cancel_after=0;cancel_after<1000;cancel_after++){
        uint32_t id,code;int32_t value;const char *error;
        CHECK(!preparation_submit(&app,d,&request,NULL,0,&id));
        unsigned step=0;uint32_t rendered=d->vm.control.contribution_frame;
        for(;step<cancel_after&&!preparation_result(&app,id,&code,&value,&error);step++){
            clock_now+=10000;app_tick(&app);
            if(!preparation_result(&app,id,&code,&value,&error)){
                CHECK(d->vm.generation==old_generation);
                if(step>10)CHECK(d->vm.control.contribution_frame>rendered);
            }
        }
        if(preparation_result(&app,id,&code,&value,&error)){
            CHECK(code==STATUS_OK&&d->vm.generation!=old_generation);total=step;break;
        }
        preparation_cancel_output(&app,"lights");
        while(preparation_pending(&app))app_tick(&app);
        CHECK(preparation_result(&app,id,&code,&value,&error)&&code!=STATUS_OK&&!strcmp(error,"cancelled"));
        CHECK(d->vm.generation==old_generation&&vm_heap_used()==baseline);
    }
    CHECK(total>1&&total<1000);
    /* Candidate allocation faults leave the displayed scene owned and runnable. */
    uint32_t id,code;int32_t value;const char *error;old_generation=d->vm.generation;
    baseline=vm_heap_used();CHECK(!preparation_submit(&app,d,&request,NULL,0,&id));deny_allocation=true;
    for(unsigned n=0;n<1000&&!preparation_result(&app,id,&code,&value,&error);n++)app_tick(&app);
    deny_allocation=false;
    CHECK(preparation_result(&app,id,&code,&value,&error)&&code==STATUS_LOAD);
    CHECK(d->vm.generation==old_generation&&vm_heap_used()==baseline);
    /* Fail each allocation in the real preparation path, including later
       resource/state buffers, and preserve the old generation and heap. */
    unsigned successful_allocation=0;
    for(unsigned nth=1;nth<100;nth++){
        old_generation=d->vm.generation;baseline=vm_heap_used();fail_allocation_at=allocations+nth;
        const char *submission=preparation_submit(&app,d,&request,NULL,0,&id);
        if(!submission){
            for(unsigned n=0;n<1000&&!preparation_result(&app,id,&code,&value,&error);n++)app_tick(&app);
            CHECK(preparation_result(&app,id,&code,&value,&error));
        }
        fail_allocation_at=0;
        if(!submission&&code==STATUS_OK){successful_allocation=nth;break;}
        CHECK(d->vm.generation==old_generation&&vm_heap_used()==baseline);
    }
    CHECK(successful_allocation>5&&successful_allocation<100);
    /* Reconfiguration cancels pending work without reusing IDs in this boot. */
    uint32_t first=id;CHECK(!preparation_submit(&app,d,&request,NULL,0,&id));CHECK(id>first);preparation_abort_all(&app);
    CHECK(!preparation_submit(&app,d,&request,NULL,0,&first)&&first>id);preparation_abort_all(&app);
    /* A single-program slot still owns a named layer, and manual lifecycle
       commands must prevent an older candidate from restarting it later. */
    const char *slot="{\"op\":\"program_activate\",\"name\":\"counter\",\"output\":\"lights\",\"slot\":\"B\"}";
    command(&session,CMD_MANAGE,(const uint8_t *)slot,strlen(slot));CHECK(reply.code==STATUS_OK);
    Composition *scene=d->transition->compositions[1];
    CHECK(!strcmp(scene->layers[0].vm.layer,"main"));
    const char *stops[]={
        "{\"op\":\"output_command\",\"output\":\"lights\",\"slot\":\"B\",\"command\":8}",
        "{\"op\":\"output_command\",\"output\":\"lights\",\"slot\":\"B\",\"layer\":\"main\",\"command\":8}"};
    CHECK(CMD_STOP==8);
    for(unsigned n=0;n<2;n++){
        request=(PreparationRequest){.kind=PREPARE_SLOT,.slot=1,.play=true,.revision=d->composition_revision,.light=d->pixels.light,.entry=*store_find(&app.store,STORE_ANIMATION,"slow")};
        strcpy(request.name,"slow");CHECK(!preparation_submit(&app,d,&request,NULL,0,&id));
        command(&session,CMD_MANAGE,(const uint8_t *)stops[n],strlen(stops[n]));CHECK(reply.code==STATUS_OK);
        /* A render turn may precede the next preparation boundary. */
        for(unsigned turn=0;turn<2&&!preparation_result(&app,id,&code,&value,&error);turn++)app_tick(&app);
        CHECK(preparation_result(&app,id,&code,&value,&error)&&code!=STATUS_OK);
        CHECK(d->transition->compositions[1]==scene);
    }
    /* Invalidation must also stop an unfinished candidate before it reaches
       commit, including a guest that would otherwise never finish init. */
    CHECK(!preparation_submit(&app,d,&request,NULL,0,&id));
    const char *stale_pause="{\"op\":\"program_pause\",\"output\":\"lights\",\"slot\":\"B\",\"layer\":\"main\",\"expected_generation\":0}";
    command(&session,CMD_MANAGE,(const uint8_t *)stale_pause,strlen(stale_pause));CHECK(reply.code==STATUS_CONFIG);
    app_tick(&app);CHECK(!preparation_result(&app,id,&code,&value,&error));
    d->composition_revision++;
    /* A render turn may precede the next preparation boundary. */
    for(unsigned turn=0;turn<2&&!preparation_result(&app,id,&code,&value,&error);turn++)app_tick(&app);
    CHECK(preparation_result(&app,id,&code,&value,&error)&&code!=STATUS_OK);
    /* The 1,024-pixel/four-scalar Pico scene left 7,336 bytes free. Even an
       empty replacement used to need both a 5 KiB parser and a 2.5 KiB config.
       Validation must fit without releasing or changing that live scene. */
    size_t reserve=VM_HEAP_LIMIT-vm_heap_used()-7336u;
    CHECK(vm_heap_charge(reserve));baseline=vm_heap_used();
    request=(PreparationRequest){.kind=PREPARE_CONFIG_VALIDATE,.active_revision=app.revision,.saved_revision=app.store.config_revision};
    CHECK(!preparation_submit(&app,d,&request,EMPTY_CONFIG,strlen(EMPTY_CONFIG),&id));
    for(unsigned n=0;n<1000&&!preparation_result(&app,id,&code,&value,&error);n++)preparation_tick(&app);
    CHECK(preparation_result(&app,id,&code,&value,&error)&&code==STATUS_OK);
    CHECK(d->transition->compositions[1]==scene&&vm_heap_used()==baseline);
    const char *invalid="{\"schema\":\"lightvm.config\",\"outputs\":[],\"unexpected\":1}";
    CHECK(!preparation_submit(&app,d,&request,invalid,strlen(invalid),&id));
    for(unsigned n=0;n<1000&&!preparation_result(&app,id,&code,&value,&error);n++)preparation_tick(&app);
    CHECK(preparation_result(&app,id,&code,&value,&error)&&code==STATUS_CONFIG);
    CHECK(d->transition->compositions[1]==scene&&vm_heap_used()==baseline);
    vm_heap_uncharge(reserve);
    session_destroy(&session);app_destroy(&app);storage_available=false;CHECK(vm_heap_used()==0);

    /* Every byte boundary is a legal parser yield; decoded duplicate keys and
       invalid exponent/fraction suffixes are still rejected. */
    const char *documents[]={"{\"a\":[-1.25e+2,true,false,null,\"x\\u0020y\",{}]}","0","[]", "{\"a\":1,\"\\u0061\":2}","[1,]","{\"a\":}","01","1e+","1.","[truefalse]"};
    for(unsigned n=0;n<sizeof(documents)/sizeof(*documents);n++)for(unsigned quantum=1;quantum<=7;quantum++){
        Json j;JsonParser parser;json_begin(&parser,&j,documents[n],strlen(documents[n]));unsigned ticks=0;int status;
        do{status=json_step(&parser,quantum);CHECK(++ticks<1000);}while(!status);
        CHECK((status>0)==(n<3));
    }
    /* Truncated status fields must remain terminated and leave both guards
       intact, including zero capacity and the largest unsigned counter. */
    for(size_t capacity=0;capacity<24;capacity++){
        unsigned char guarded[32];memset(guarded,0xa5,sizeof(guarded));
        StatusWriter writer={(char *)guarded+4,capacity,0};
        status_integer(&writer,"counter",UINT64_MAX);
        status_field(&writer,"suffix","value",5);
        for(size_t n=0;n<4;n++)CHECK(guarded[n]==0xa5);
        for(size_t n=4+capacity;n<sizeof(guarded);n++)CHECK(guarded[n]==0xa5);
        if(capacity)CHECK(guarded[4+capacity-1]==0);
    }
    /* Wire CRC remains identical for every byte and arbitrary incremental
       boundaries, including nonstandard seeds used by streaming callers. */
    CHECK(crc32_bytes((const uint8_t *)"123456789",9)==0xcbf43926u);
    uint8_t crc_data[256];for(unsigned i=0;i<256;i++)crc_data[i]=(uint8_t)i;
    const uint32_t seeds[]={0,UINT32_MAX,0x12345678u};
    for(unsigned seed=0;seed<3;seed++){
        uint32_t reference=seeds[seed];
        for(unsigned size=0;size<=256;size++){
            CHECK(crc32_update(seeds[seed],crc_data,size)==reference);
            unsigned split=size/2;
            CHECK(crc32_update(crc32_update(seeds[seed],crc_data,split),crc_data+split,size-split)==reference);
            if(size<256){reference^=crc_data[size];for(unsigned bit=0;bit<8;bit++)reference=(reference>>1)^(0xedb88320u&(0u-(reference&1u)));}
        }
    }
    /* Fast diagnostic formatting retains the full unsigned 64-bit domain. */
    uint64_t integers[]={0,9,10,99,100,UINT32_MAX,(uint64_t)UINT32_MAX+1,UINT64_MAX};
    for(unsigned n=0;n<sizeof(integers)/sizeof(*integers);n++){
        char actual[21],expected[21];size_t size=json_format_u64(actual,integers[n]);
        snprintf(expected,sizeof(expected),"%llu",(unsigned long long)integers[n]);
        CHECK(!strcmp(actual,expected)&&size==strlen(expected));
    }
    for(uint64_t n=1;n<UINT64_MAX/10;n*=10){
        char actual[21],expected[21];json_format_u64(actual,n-1);
        snprintf(expected,sizeof(expected),"%llu",(unsigned long long)(n-1));CHECK(!strcmp(actual,expected));
    }
    Json oversized;JsonParser parser;
    char children[513];children[0]='[';
    for(unsigned n=0;n<256;n++){children[1+n*2]='0';children[2+n*2]=',';}
    children[510]=']';CHECK(json_parse(&oversized,children,511));
    CHECK(oversized.count==256&&oversized.tokens[0].children==255&&oversized.tokens[0].next==256);
    children[510]=',';children[512]=']';CHECK(!json_parse(&oversized,children,513));
    char *boundary=malloc(UINT16_MAX);CHECK(boundary);
    memset(boundary,' ',UINT16_MAX);boundary[UINT16_MAX-1]='0';
    json_begin(&parser,&oversized,boundary,UINT16_MAX);
    int status;do{status=json_step(&parser,256);}while(!status);
    CHECK(status>0&&oversized.tokens[0].start==UINT16_MAX-1&&oversized.tokens[0].end==UINT16_MAX);
    free(boundary);
    json_begin(&parser,&oversized,"0",(size_t)UINT16_MAX+1u);
    CHECK(json_step(&parser,1)<0); /* Reject before reading or truncating offsets. */
}
