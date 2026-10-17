/* A scene may prepare its next frame while the output driver owns the front. */
#include "scene.h"
static void scene_contract(void){
    clock_now=0;clock_step=0;storage_available=false;deny_allocation=false;deny_show=false;
    CHECK(vm_heap_used()==0);
    PixelBuffer physical={0};PixelConfig config={16,67,255,"RGB"};
    CHECK(pixels_init(&physical,&config,device_type("ws2812b")));
    Scene *s=scene_create(&physical,2);CHECK(s);
    Capture reply={.marker=0x12345678u};
    for(unsigned i=0;i<2;i++){
        portable_load(&s->layers[i].vm,&s->layers[i].target,&reply,counter_lbc,sizeof(counter_lbc));
        s->layers[i].vm.playing=true;
    }
    scene_prepare(s);
    unsigned allocation_count=allocations;
    deny_allocation=true;delayed_output=true;output_ready=false;output_failed=false;
    clock_now=20000;scene_tick(s,100);
    CHECK(s->pending&&borrowed_output==s->front&&physical.rgb[0]==0);
    /* Finish several composites while the original front is borrowed. Every
       pixel must use the same committed layer generation across all slices. */
    for(unsigned n=0;n<12;n++){
        clock_now+=20000;uint64_t rendered=s->layers[1].vm.portable->render_count;
        bool composing=s->composing;scene_tick(s,100);
        if(composing)CHECK(s->layers[1].vm.portable->render_count==rendered);
        CHECK(s->pending&&physical.rgb[0]==0);
        if(s->prepared)for(unsigned p=1;p<s->count;p++)CHECK(!memcmp(s->working,s->working+p*3,3));
    }
    output_ready=true;scene_tick(s,100);CHECK(s->frames>0&&physical.rgb[0]>0&&!borrowed_output);
    for(unsigned n=0;n<15;n++){clock_now+=20000;scene_tick(s,100);}
    CHECK(s->frames>1&&!s->layers[0].vm.control.faulted);
    output_ready=false;
    for(unsigned n=0;n<10&&!s->pending;n++){clock_now+=20000;scene_tick(s,100);}
    CHECK(s->pending);uint32_t frames=s->frames;output_failed=true;scene_tick(s,100);
    CHECK(s->frames==frames&&!s->layers[0].vm.control.faulted);
    output_failed=false;
    for(unsigned n=0;n<10&&!s->pending;n++){clock_now+=20000;scene_tick(s,100);}
    CHECK(s->pending);unsigned cancelled=cancelled_outputs;
    CHECK(allocations==allocation_count);deny_allocation=false;
    scene_destroy(s);CHECK(!borrowed_output&&cancelled_outputs==cancelled+1);
    delayed_output=false;pixels_destroy(&physical);CHECK(vm_heap_used()==0);

    /* Closing a physical output can itself service the engine. Detach its scene
       before freeing it, so those checkpoints cannot reenter freed state. */
    storage_available=true;memset(flash,255,sizeof(flash));
    static App app;Session session;app_init(&app,capture,&reply);
    session_init(&session,&app,1,capture,&reply);
    secondary_output_allowed=true;
    const char *config_json="{\"schema\":\"lightvm.config/1\",\"devices\":[{\"id\":\"lights\",\"type\":\"ws2812b\",\"config\":{\"pin\":\"GP16\",\"count\":3}},{\"id\":\"other\",\"type\":\"ws2812b\",\"config\":{\"pin\":\"GP17\",\"count\":3}}]}";
    memcpy(app.staging,config_json,strlen(config_json));app.expected=(uint32_t)strlen(config_json);
    app.staged=true;app.object_kind=1;app.upload_owner=1;
    const char *apply="{\"op\":\"config_apply\",\"expected_revision\":0}";
    command(&session,CMD_MANAGE,(const uint8_t *)apply,strlen(apply));CHECK(reply.code==STATUS_OK);
    /* Activation presents its initial frame before autonomous playback. The
       direct driver must receive RGB, never the RGBA contribution bytes. */
    CHECK(store_put(&app.store,"counter",counter_lbc,sizeof(counter_lbc)));
    const char *activate="{\"op\":\"procedure_activate\",\"name\":\"counter\",\"device\":\"lights\",\"action\":\"play\"}";
    command(&session,CMD_MANAGE,(const uint8_t *)activate,strlen(activate));CHECK(reply.code==STATUS_OK);
    for(unsigned i=0;i<3;i++)CHECK(displayed[i*3]==1&&displayed[i*3+1]==0&&displayed[i*3+2]==0);
    CHECK(!memcmp(app.devices[0]->pixels.rgb,displayed,9));
    for(unsigned i=0;i<app.device_count;i++){
        AppDevice *d=app.devices[i];d->scene=scene_create(&d->pixels,1);CHECK(d->scene);
        portable_load(&d->scene->layers[0].vm,&d->scene->layers[0].target,&reply,counter_lbc,sizeof(counter_lbc));
        d->scene->layers[0].vm.playing=true;d->vm.output=NULL;
    }
    memcpy(app.staging,EMPTY_CONFIG,strlen(EMPTY_CONFIG));app.expected=(uint32_t)strlen(EMPTY_CONFIG);
    app.staged=true;app.object_kind=1;app.upload_owner=1;cooperative_show=true;
    apply="{\"op\":\"config_apply\",\"expected_revision\":1}";
    command(&session,CMD_MANAGE,(const uint8_t *)apply,strlen(apply));CHECK(reply.code==STATUS_OK);
    cooperative_show=false;secondary_output_allowed=false;session_destroy(&session);app_destroy(&app);storage_available=false;CHECK(vm_heap_used()==0);
}
