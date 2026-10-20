/* The monotonic driver and output ownership are independent of guest clocks. */
#include "transition.h"
#include <math.h>
/* Advance the virtual clock between scheduler rounds, also in capped builds. */
static void transition_test_tick(Transition *t,unsigned budget){
    clock_now+=FRAME_INTERVAL_US;transition_tick(t,budget);
}
static void startup_transition_clock(void){
    CHECK(vm_heap_used()==0);clock_now=0;clock_step=0;storage_available=true;
    deny_allocation=false;deny_show=false;memset(flash,255,sizeof(flash));
    Store store;CHECK(store_mount(&store));
    CHECK(store_put(&store,STORE_ANIMATION,"paint",library_lbc,sizeof(library_lbc)));
    CHECK(store_put(&store,STORE_TRANSITION,"fade",transition_lbc,sizeof(transition_lbc)));
    char preset[512],config[512];
    int size=snprintf(preset,sizeof(preset),"{\"schema\":\"lightvm.preset\",\"layers\":[{\"id\":\"main\",\"program\":\"paint\",\"crc32\":%lu}],\"entry_transition\":{\"program\":\"fade\",\"crc32\":%lu,\"duration_ms\":1500}}",
        (unsigned long)crc32_bytes(library_lbc,sizeof(library_lbc)),(unsigned long)crc32_bytes(transition_lbc,sizeof(transition_lbc)));
    CHECK(size>0&&(size_t)size<sizeof(preset));CHECK(store_put(&store,STORE_PRESET,"scene",preset,(uint32_t)size));
    size=snprintf(config,sizeof(config),"{\"schema\":\"lightvm.config\",\"outputs\":[{\"id\":\"lights\",\"type\":\"ws2812b\",\"config\":{\"pin\":\"GP16\",\"count\":5},\"startup\":{\"mode\":\"preset\",\"preset\":\"scene\"}}]}");
    CHECK(size>0&&(size_t)size<sizeof(config));CHECK(store_put_config(&store,config,(uint32_t)size));store_destroy(&store);
    static App app;Capture reply={.marker=0x12345678u};app_init(&app,capture,&reply);
    CHECK(!app.boot_error[0]&&app.output_count==1);Transition *t=app.outputs[0]->transition;CHECK(t&&t->driver==TRANSITION_TIMED);
    /* CYW43/network setup can take longer than the entire configured fade.
       Neither that gap nor cooperative ingress work may consume startup time. */
    clock_now=5000000;runtime_checkpoint();transition_update(t,clock_now);
    CHECK(t->progress==0&&t->frames==0);
    app_tick(&app);CHECK(t->progress==0&&t->driver==TRANSITION_TIMED);
    for(unsigned n=0;n<30&&t->frames<1;n++)app_tick(&app);
    CHECK(t->frames>=1&&app.outputs[0]->pixels.rgb[2]==0);
    clock_now+=750000;for(unsigned n=0;n<30;n++)app_tick(&app);
    CHECK(t->progress==50&&t->driver==TRANSITION_TIMED);
    CHECK(app.outputs[0]->pixels.rgb[2]>0);
    clock_now+=750000;for(unsigned n=0;n<30;n++)app_tick(&app);
    CHECK(t->progress==100&&t->driver==TRANSITION_MANUAL);
    app_destroy(&app);storage_available=false;CHECK(vm_heap_used()==0);
}
static void transition_contract(void){
    clock_now=0;clock_step=0;deny_allocation=false;deny_show=false;
    CHECK(vm_heap_used()==0);
    PixelBuffer physical={0};PixelConfig config={.pin=16,.count=131,.brightness=255,.order="RGB",.clock_pin=-1,.enable_pin=-1};
    CHECK(pixels_init(&physical,&config,output_type("ws2812b")));
    Transition *t=transition_create(&physical);CHECK(t);
    CHECK(transition_set(t,35));CHECK(transition_run(t,100,2000));
    transition_update(t,1000000);CHECK(t->progress==67.5&&t->driver==TRANSITION_TIMED);
    CHECK(!transition_run(t,50,-1));CHECK(!transition_run(t,NAN,10));CHECK(!transition_run(t,50,INFINITY));
    CHECK(t->progress==67.5&&t->driver==TRANSITION_TIMED);
    transition_update(t,2000000);CHECK(t->progress==100&&t->driver==TRANSITION_MANUAL);
    clock_now=2000000;CHECK(transition_run(t,0,2000));
    transition_update(t,3000000);CHECK(t->progress==50);
    clock_now=3000000;CHECK(transition_run(t,100,2000));
    transition_update(t,4000000);CHECK(t->progress==75);
    clock_now=4000000;transition_hold(t);transition_update(t,9999999);CHECK(t->progress==75);
    CHECK(transition_assign(t,"sensor"));uint32_t token=t->assignment;
    CHECK(transition_source(t,"sensor",token,35));CHECK(!transition_source(t,"sensor",token,NAN));
    CHECK(!transition_source(t,"other",token,50)&&t->progress==35);
    transition_hold(t);CHECK(!transition_source(t,"sensor",token,50));
    CHECK(transition_assign(t,"sensor"));CHECK(t->assignment!=token);CHECK(!transition_source(t,"sensor",token,50));
    CHECK(transition_set(t,0));CHECK(t->driver==TRANSITION_MANUAL);

    /* A repeating routine evaluates the current leg directly after any gap. */
    clock_now=10000000;CHECK(transition_set(t,0));CHECK(transition_run_interval(t,100,2000,10000));
    transition_update(t,11000000);CHECK(t->progress==50);
    transition_update(t,12000000);CHECK(t->progress==100&&t->driver==TRANSITION_TIMED);
    transition_update(t,19000000);CHECK(t->progress==100);
    transition_update(t,21000000);CHECK(t->progress==50);
    transition_update(t,22000000);CHECK(t->progress==0);
    transition_update(t,51000000);CHECK(t->progress==50&&t->driver==TRANSITION_TIMED);
    CHECK(!transition_run_interval(t,100,2000,1000));CHECK(!transition_run_interval(t,100,2000,NAN));
    CHECK(t->progress==50&&t->interval==10000000);
    clock_now=52000000;transition_hold(t);transition_update(t,91000000);CHECK(t->progress==100&&t->driver==TRANSITION_MANUAL);

    for(unsigned n=0;n<2;n++){
        t->compositions[n]=composition_create(&t->target,0);CHECK(t->compositions[n]);
        memset(t->compositions[n]->front,n?200:100,131*3);t->compositions[n]->playing=false;
    }
    /* A management mutation can invalidate a suspended custom call. Restoring
       its baseline must also discard that call's interpreter continuation. */
    Capture custom_reply={0};TransitionProgram *custom=demo_calloc(1,sizeof(*custom));CHECK(custom);
    portable_load(&custom->vm,&t->target,&custom_reply,counter_lbc,sizeof(counter_lbc));
    CHECK(transition_program_capture(custom));t->programs[0]=custom;t->effect=TRANSITION_CUSTOM;
    CHECK(transition_set(t,35));
    for(unsigned n=0;n<100&&!custom->vm.control.in_step;n++)transition_test_tick(t,1);
    CHECK(custom->vm.control.in_step);transition_invalidate(t);
    CHECK(!transition_frame(t,0)&&!custom->vm.control.faulted);
    transition_program_destroy(custom);t->programs[0]=NULL;t->effect=TRANSITION_CROSSFADE;
    transition_invalidate(t);t->frames=0;memset(physical.rgb,0,131*3);
    for(unsigned n=0;n<2;n++)memset(t->compositions[n]->front,n?200:100,131*3);
    CHECK(transition_set(t,35));
    unsigned allocation_count=allocations;deny_allocation=true;
    delayed_output=true;output_ready=false;output_failed=false;
    /* First sample spans multiple slices. Later updates must not tear that frame. */
    transition_test_tick(t,64);CHECK(t->composing&&t->sampled_progress==35);
    CHECK(transition_set(t,90));while(t->composing)transition_test_tick(t,64);
    CHECK(t->prepared);
    for(unsigned n=0;n<131*3;n++)CHECK(t->working[n]==135);
    transition_test_tick(t,64);CHECK(t->pending&&borrowed_output==physical.dimmed&&t->frames==0);
    for(unsigned n=0;n<12;n++){CHECK(transition_set(t,n*5));transition_test_tick(t,64);CHECK(t->pending&&t->frames==0);}
    output_ready=true;t->command_active=true;transition_test_tick(t,64);t->command_active=false;CHECK(t->frames==1&&physical.rgb[0]==135);
    output_ready=false;transition_test_tick(t,64);transition_test_tick(t,64);transition_test_tick(t,64);transition_test_tick(t,64);
    CHECK(t->pending);uint32_t frames=t->frames;output_failed=true;transition_test_tick(t,64);
    CHECK(t->frames==frames&&physical.rgb[0]==135);output_failed=false;
    for(unsigned n=0;n<8&&!t->pending;n++)transition_test_tick(t,64);
    CHECK(t->pending);CHECK(allocations==allocation_count);deny_allocation=false;
    composition_destroy(t->compositions[0]);composition_destroy(t->compositions[1]);
    unsigned cancelled=cancelled_outputs;transition_destroy(t);CHECK(!borrowed_output&&cancelled_outputs==cancelled+1);
    delayed_output=false;pixels_destroy(&physical);CHECK(vm_heap_used()==0);

    /* A suspended Python transition pins only source publication. Composition VMs
       and back-buffer composition keep running, without tearing the sample. */
    clock_now=0;CHECK(pixels_init(&physical,&config,output_type("ws2812b")));
    t=transition_create(&physical);CHECK(t);t->effect=TRANSITION_PROGRAM;
    for(unsigned n=0;n<2;n++){
        Composition *s=composition_create(&t->target,1);CHECK(s);t->compositions[n]=s;
        portable_load(&s->layers[0].vm,&s->layers[0].target,&custom_reply,counter_lbc,sizeof(counter_lbc));
        s->layers[0].vm.playing=true;composition_prepare(s);composition_publish(s);
    }
    custom=demo_calloc(1,sizeof(*custom));CHECK(custom);
    portable_load(&custom->vm,&t->target,&custom_reply,transition_lbc,sizeof(transition_lbc));
    CHECK(transition_program_capture(custom));t->programs[0]=custom;
    CHECK(transition_set(t,35));allocation_count=allocations;deny_allocation=true;
    transition_test_tick(t,1);CHECK(t->composing&&t->compositions[0]->front_pinned);
    uint8_t sampled=t->compositions[0]->front[0];
    for(unsigned n=0;n<100&&t->phase<4;n++){
        clock_now+=20000;transition_test_tick(t,1);
        if(t->phase<4)CHECK(t->compositions[0]->front_pinned&&t->compositions[0]->front[0]==sampled);
    }
    CHECK(t->composing&&t->phase==4);
    CHECK(!t->compositions[0]->front_pinned);
    Composition *s=t->compositions[0];composition_invalidate(s);s->layers[0].vm.next_frame=0;
    uint64_t renders=s->layers[0].vm.portable->render_count;
    clock_now+=20000;transition_test_tick(t,64);
    CHECK(s->layers[0].vm.portable->render_count>renders);
    CHECK(!s->front_pinned);
    /* Publish a different source while RGB conversion is unfinished. The
       remaining output pixels must still come from the committed program. */
    composition_prepare(s);composition_publish(s);CHECK(s->front[0]!=sampled);
    for(unsigned n=0;n<100&&t->composing;n++){clock_now+=20000;transition_test_tick(t,64);}
    CHECK(t->prepared&&!s->front_pinned);
    for(unsigned n=0;n<131;n++)CHECK(t->working[n*3]==sampled&&t->working[n*3+1]==0&&t->working[n*3+2]==0);
    CHECK(allocations==allocation_count);deny_allocation=false;
    transition_invalidate(t);composition_destroy(t->compositions[0]);composition_destroy(t->compositions[1]);
    transition_destroy(t);pixels_destroy(&physical);CHECK(vm_heap_used()==0);
}
