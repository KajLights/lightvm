/* A composition may prepare its next frame while the output driver owns the front. */
#include "composition.h"
static void composition_contract(void){
    clock_now=0;clock_step=0;storage_available=false;deny_allocation=false;deny_show=false;
    CHECK(vm_heap_used()==0);
    PixelBuffer physical={0};PixelConfig config={.pin=16,.count=67,.brightness=255,.order="RGB",.clock_pin=-1,.enable_pin=-1};
    CHECK(pixels_init(&physical,&config,output_type("ws2812b")));
    Composition *s=composition_create(&physical,2);CHECK(s);
    Capture reply={.marker=0x12345678u};
    for(unsigned i=0;i<2;i++){
        portable_load(&s->layers[i].vm,&s->layers[i].target,&reply,counter_lbc,sizeof(counter_lbc));
        s->layers[i].vm.playing=true;
    }
    composition_prepare(s);
    unsigned allocation_count=allocations;
    deny_allocation=true;delayed_output=true;output_ready=false;output_failed=false;
    clock_now=20000;composition_tick(s,100);
    CHECK(s->pending&&borrowed_output==s->front&&physical.rgb[0]==0);
    /* Finish several composites while the original front is borrowed. Every
       pixel must use the same committed layer generation across all slices. */
    for(unsigned n=0;n<12;n++){
        clock_now+=20000;uint64_t rendered=s->layers[1].vm.portable->render_count;
        bool composing=s->composing;composition_tick(s,100);
        if(composing)CHECK(s->layers[1].vm.portable->render_count==rendered);
        CHECK(s->pending&&physical.rgb[0]==0);
        if(s->prepared)for(unsigned p=1;p<s->count;p++)CHECK(!memcmp(s->working,s->working+p*3,3));
    }
    output_ready=true;composition_tick(s,100);CHECK(s->frames>0&&physical.rgb[0]>0&&!borrowed_output);
    for(unsigned n=0;n<15;n++){clock_now+=20000;composition_tick(s,100);}
    CHECK(s->frames>1&&!s->layers[0].vm.control.faulted);
    output_ready=false;
    for(unsigned n=0;n<10&&!s->pending;n++){clock_now+=20000;composition_tick(s,100);}
    CHECK(s->pending);uint32_t frames=s->frames;output_failed=true;composition_tick(s,100);
    CHECK(s->frames==frames&&!s->layers[0].vm.control.faulted);
    output_failed=false;
    for(unsigned n=0;n<10&&!s->pending;n++){clock_now+=20000;composition_tick(s,100);}
    CHECK(s->pending);unsigned cancelled=cancelled_outputs;
    CHECK(allocations==allocation_count);deny_allocation=false;
    composition_destroy(s);CHECK(!borrowed_output&&cancelled_outputs==cancelled+1);
    delayed_output=false;pixels_destroy(&physical);CHECK(vm_heap_used()==0);

    /* A completed back buffer blocks further writers until it can publish.
       This must not discard a suspended call or lose elapsed animation time. */
    PixelBuffer offscreen={.count=67};s=composition_create(&offscreen,1);CHECK(s);
    portable_load(&s->layers[0].vm,&s->layers[0].target,&reply,counter_lbc,sizeof(counter_lbc));
    s->layers[0].vm.playing=true;composition_prepare(s);composition_publish(s);s->front_pinned=true;
    for(unsigned n=0;n<40&&!s->prepared;n++){clock_now+=20000;composition_tick(s,64);}
    CHECK(s->prepared&&!s->composing);
    uint64_t rendered=s->layers[0].vm.portable->render_count,program_us=s->layers[0].vm.control.program_us;
    uint32_t front_crc=crc32_bytes(s->front,67*3),back_crc=crc32_bytes(s->working,67*3),published=s->frames;
    allocation_count=allocations;deny_allocation=true;
    for(unsigned n=0;n<20;n++){clock_now+=20000;composition_tick(s,64);}
    CHECK(s->layers[0].vm.portable->render_count==rendered&&s->frames==published);
    CHECK(crc32_bytes(s->front,67*3)==front_crc&&crc32_bytes(s->working,67*3)==back_crc);
    s->front_pinned=false;composition_tick(s,64);CHECK(s->frames==published+1);
    CHECK(crc32_bytes(s->front,67*3)==back_crc);
    clock_now+=20000;composition_tick(s,64);
    CHECK(s->layers[0].vm.portable->render_count>rendered);
    CHECK(s->layers[0].vm.control.program_us>=program_us+400000);
    CHECK(allocations==allocation_count);deny_allocation=false;composition_destroy(s);CHECK(vm_heap_used()==0);

    /* Closing a physical output can itself service the engine. Detach its composition
       before freeing it, so those checkpoints cannot reenter freed state. */
    storage_available=true;memset(flash,255,sizeof(flash));
    static App app;Session session;app_init(&app,capture,&reply);
    session_init(&session,&app,1,capture,&reply);
    secondary_output_allowed=true;
    const char *config_json="{\"schema\":\"lightvm.config\",\"outputs\":[{\"id\":\"lights\",\"type\":\"ws2812b\",\"config\":{\"pin\":\"GP16\",\"count\":3}},{\"id\":\"other\",\"type\":\"ws2812b\",\"config\":{\"pin\":\"GP17\",\"count\":3}}]}";
    memcpy(app.staging,config_json,strlen(config_json));app.expected=(uint32_t)strlen(config_json);
    app.staged=true;app.object_kind=1;app.upload_owner=1;
    const char *apply="{\"op\":\"config_apply\",\"expected_revision\":0}";
    command(&session,CMD_MANAGE,(const uint8_t *)apply,strlen(apply));CHECK(reply.code==STATUS_OK);
    /* Activation presents its initial frame before autonomous playback. The
       direct driver must receive RGB, never the RGBA contribution bytes. */
    CHECK(store_put(&app.store,STORE_ANIMATION,"counter",counter_lbc,sizeof(counter_lbc)));
    const char *activate="{\"op\":\"program_activate\",\"name\":\"counter\",\"output\":\"lights\",\"action\":\"play\"}";
    command(&session,CMD_MANAGE,(const uint8_t *)activate,strlen(activate));CHECK(reply.code==STATUS_OK);
    for(unsigned i=0;i<3;i++)CHECK(displayed[i*3]==1&&displayed[i*3+1]==0&&displayed[i*3+2]==0);
    CHECK(!memcmp(app.outputs[0]->pixels.rgb,displayed,9));
    for(unsigned i=0;i<app.output_count;i++){
        AppOutput *d=app.outputs[i];d->composition=composition_create(&d->pixels,1);CHECK(d->composition);
        portable_load(&d->composition->layers[0].vm,&d->composition->layers[0].target,&reply,counter_lbc,sizeof(counter_lbc));
        d->composition->layers[0].vm.playing=true;d->vm.output=NULL;
    }
    memcpy(app.staging,EMPTY_CONFIG,strlen(EMPTY_CONFIG));app.expected=(uint32_t)strlen(EMPTY_CONFIG);
    app.staged=true;app.object_kind=1;app.upload_owner=1;cooperative_show=true;
    apply="{\"op\":\"config_apply\",\"expected_revision\":1}";
    command(&session,CMD_MANAGE,(const uint8_t *)apply,strlen(apply));CHECK(reply.code==STATUS_OK);
    cooperative_show=false;secondary_output_allowed=false;session_destroy(&session);app_destroy(&app);storage_available=false;CHECK(vm_heap_used()==0);
}
