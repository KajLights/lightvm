/* Deterministic suspension/publication contract against the real interpreter. */
#include "animation.h"
static void inputs_contract(void){
    VmInstance vm;uint8_t canvas[9]={0};PixelBuffer target={.rgb=canvas,.count=3};
    vm_init(&vm,&target,NULL,NULL);vm.output=NULL;
    vm.memory=inputs_lbc;vm.expected=sizeof(inputs_lbc);
    CHECK(!vm_load(&vm));vm.ready=true;CHECK(!control_prepare(&vm));
    InputRegistry registry={0};vm.portable->input_registry=&registry;Json j;uint32_t publisher;
    const char *claim="[{\"name\":\"test.level\",\"type\":\"float\"}]";
    CHECK(json_parse(&j,claim,strlen(claim)));CHECK(!input_registry_claim(&registry,&j,0,&publisher));
    const char *binding="{\"level\":{\"source\":\"test.level\",\"timeout_ms\":0}}";
    CHECK(json_parse(&j,binding,strlen(binding)));CHECK(!inputs_bind(vm.portable,&j,0,0));
    const char *first="{\"test.level\":0.2}",*second="{\"test.level\":0.8}";
    CHECK(json_parse(&j,first,strlen(first)));CHECK(!input_registry_publish(&registry,&j,0,publisher,1));
    bool ready=false;unsigned before=allocations;deny_allocation=true;
    CHECK(!animation_poll_budget(&vm,0,&ready,1)&&!ready);
    CHECK(json_parse(&j,second,strlen(second)));CHECK(!input_registry_publish(&registry,&j,0,publisher,2));
    for(unsigned n=0;!ready&&n<1000;n++)CHECK(!animation_poll_budget(&vm,0,&ready,1));
    CHECK(ready);
    for(unsigned n=0;n<3;n++)CHECK(vm.control.committed[n*4]==20);
    CHECK(!control_step(&vm,0));for(unsigned n=0;n<3;n++)CHECK(vm.control.committed[n*4]==80);
    CHECK(allocations==before);deny_allocation=false;
    /* Replacing wiring also preserves a suspended invocation's old snapshot. */
    CHECK(!animation_poll_budget(&vm,0,&ready,1)&&!ready);
    const char *constant="{\"level\":{\"value\":0.6}}";
    CHECK(json_parse(&j,constant,strlen(constant)));CHECK(!inputs_bind(vm.portable,&j,0,0));
    before=allocations;deny_allocation=true;
    for(unsigned n=0;!ready&&n<1000;n++)CHECK(!animation_poll_budget(&vm,0,&ready,1));
    CHECK(ready);for(unsigned n=0;n<3;n++)CHECK(vm.control.committed[n*4]==80);
    CHECK(!control_step(&vm,0));for(unsigned n=0;n<3;n++)CHECK(vm.control.committed[n*4]==60);
    CHECK(allocations==before);deny_allocation=false;
    /* Built-in light controls follow the same invocation snapshot boundary. */
    LightControls light={.brightness=0.3,.color=0xffffff};
    inputs_attach(vm.portable,&registry,&light);
    const char *control="{\"level\":{\"control\":\"brightness\"}}";
    CHECK(json_parse(&j,control,strlen(control)));CHECK(!inputs_bind(vm.portable,&j,0,0));
    CHECK(!animation_poll_budget(&vm,0,&ready,1)&&!ready);
    light.brightness=0.9;
    for(unsigned n=0;!ready&&n<1000;n++)CHECK(!animation_poll_budget(&vm,0,&ready,1));
    CHECK(ready);for(unsigned n=0;n<3;n++)CHECK(vm.control.committed[n*4]==30);
    CHECK(!control_step(&vm,0));for(unsigned n=0;n<3;n++)CHECK(vm.control.committed[n*4]==90);
    /* Submitted snapshots retain their input revision across delayed output.
       Displaying an older revision cannot clear newer dirty input; unchanged
       dynamic frames cannot inflate the last input-to-display latency. */
    clock_now=100;clock_step=0;control_observe(&vm,0);
    ContributionStamp old_stamp=vm.control.contribution;
    old_stamp.generation=vm.generation;old_stamp.frame=100;
    old_stamp.dependency_revision=vm.control.dependency_revision;old_stamp.dirty_since_us=100;
    vm.control.dirty=true;vm.control.dirty_since_us=100;
    light.brightness=0.4;clock_now=200;control_observe(&vm,0);
    CHECK(vm.control.dependency_revision>old_stamp.dependency_revision);
    control_contribution_presented(&vm,old_stamp,300);
    CHECK(vm.control.dirty&&vm.control.dirty_to_display_us==200);
    ContributionStamp current=old_stamp;current.frame++;
    current.dependency_revision=vm.control.dependency_revision;
    control_contribution_presented(&vm,current,400);
    CHECK(!vm.control.dirty&&vm.control.dirty_to_display_us==300);
    current.frame++;control_contribution_presented(&vm,current,900);
    CHECK(vm.control.dirty_to_display_us==300);
    uint64_t runnable=vm.control.wait_us[1],output_wait=vm.control.wait_us[3];
    clock_now=1000;control_wait(&vm,1);clock_now=1100;control_wait(&vm,3);clock_now=1300;control_wait(&vm,0);
    CHECK(vm.control.wait_us[1]==runnable+100&&vm.control.wait_us[3]==output_wait+200);
    vm_destroy(&vm);input_registry_destroy(&registry);CHECK(vm_heap_used()==0);

    /* Sleeping contributions still have a live clock. New ramps begin at the
       binding time, and external timeout invalidation works without renders. */
    clock_now=0;clock_step=0;vm_init(&vm,&target,NULL,NULL);vm.output=NULL;
    vm.memory=reusable_inputs_lbc;vm.expected=sizeof(reusable_inputs_lbc);
    CHECK(!vm_load(&vm));vm.ready=true;CHECK(!control_prepare(&vm));
    CHECK(vm.portable->render_reusable);vm.playing=true;
    uint32_t initial_frame=vm.control.contribution_frame;
    for(unsigned n=1;n<=30;n++){clock_now=n*1000000u;vm_tick(&vm);}
    CHECK(vm.control.program_us==30000000&&vm.control.contribution_frame==initial_frame);
    const char *ramp="{\"level\":{\"driver\":{\"kind\":\"ramp\",\"duration\":1.0,\"from\":0.0,\"to\":1.0,\"restart\":true}}}";
    CHECK(json_parse(&j,ramp,strlen(ramp)));CHECK(!inputs_bind(vm.portable,&j,0,vm.control.program_us));
    clock_now+=50000;vm_tick(&vm);CHECK(vm.control.committed[0]==5);
    clock_now+=450000;vm_tick(&vm);CHECK(vm.control.committed[0]==50);
    vm.portable->input_registry=&registry;
    CHECK(json_parse(&j,claim,strlen(claim)));CHECK(!input_registry_claim(&registry,&j,0,&publisher));
    const char *expires="{\"level\":{\"source\":\"test.level\",\"timeout_ms\":100}}";
    CHECK(json_parse(&j,expires,strlen(expires)));CHECK(!inputs_bind(vm.portable,&j,0,vm.control.program_us));
    CHECK(json_parse(&j,second,strlen(second)));CHECK(!input_registry_publish(&registry,&j,0,publisher,1));
    clock_now+=20000;vm_tick(&vm);CHECK(vm.control.committed[0]==80);
    initial_frame=vm.control.contribution_frame;clock_now+=20000;vm_tick(&vm);
    CHECK(vm.control.contribution_frame==initial_frame);
    clock_now+=100000;vm_tick(&vm);CHECK(vm.control.committed[0]==20&&vm.control.contribution_frame>initial_frame);
    vm_destroy(&vm);input_registry_destroy(&registry);CHECK(vm_heap_used()==0);

    /* A first/completed native chunk has no bulk flag. Do not admit its
       expensive operation after the slice deadline; retain progress to resume. */
    vm_init(&vm,&target,NULL,NULL);vm.output=NULL;vm.memory=counter_lbc;vm.expected=sizeof(counter_lbc);
    CHECK(!vm_load(&vm));vm.ready=true;CHECK(!control_prepare(&vm));
    clock_step=LIGHTVM_RENDER_SLICE_US;
    CHECK(!animation_poll_budget(&vm,0,&ready,4096)&&!ready);
    clock_step=0;
    CHECK(!animation_poll_budget(&vm,0,&ready,4096)&&ready);
    vm_destroy(&vm);CHECK(vm_heap_used()==0);

    /* Before pinning another composite, give a sleeping reusable layer one
       admission turn to sample changed inputs. Existing composed work remains
       publishable; this does not cancel an invocation or its captured inputs. */
    PixelBuffer offscreen={.count=3};Composition *scene=composition_create(&offscreen,1);CHECK(scene);
    VmInstance *reactive=&scene->layers[0].vm;
    vm_init(reactive,&scene->layers[0].target,NULL,NULL);reactive->output=NULL;
    reactive->memory=reusable_inputs_lbc;reactive->expected=sizeof(reusable_inputs_lbc);
    CHECK(!vm_load(reactive));reactive->ready=true;CHECK(!control_prepare(reactive));reactive->playing=true;
    composition_prepare(scene);composition_publish(scene);scene->dirty=true;
    CHECK(json_parse(&j,constant,strlen(constant)));CHECK(!inputs_bind(reactive->portable,&j,0,0));
    vm_tick_begin();composition_collect_ready(scene);CHECK(!scene->composing&&!scene->prepared);
    CHECK(vm_tick_collect(reactive,4096));vm_tick_flush();composition_tick(scene,4096);vm_tick_end();
    CHECK(scene->prepared);for(unsigned n=0;n<3;n++)CHECK(scene->working[n*3]==60);
    scene->front_pinned=true;uint32_t back_crc=crc32_bytes(scene->working,9);
    const char *next_input="{\"level\":{\"value\":0.4}}";
    CHECK(json_parse(&j,next_input,strlen(next_input)));CHECK(!inputs_bind(reactive->portable,&j,0,0));
    static App collecting;memset(&collecting,0,sizeof(collecting));
    network_settings_init(&collecting.network_settings,&collecting.network,&collecting.store);
    AppOutput collected={.composition=scene};AppOutput *collected_outputs[]={&collected};
    collecting.outputs=collected_outputs;collecting.output_count=1;
    /* The next render must also respect a board's optional refresh cap. */
    clock_now=reactive->next_frame;
    app_tick(&collecting);
    CHECK(scene->prepared&&crc32_bytes(scene->working,9)==back_crc);
    for(unsigned n=0;n<3;n++)CHECK(reactive->control.committed[n*4]==40);
    composition_destroy(scene);CHECK(vm_heap_used()==0);

    /* Management must not cancel an unfinished transition on every slider update. */
    Capture reply={.marker=0x12345678u};
    Transition *transition=transition_create(&target);CHECK(transition);
    TransitionProgram *program=demo_calloc(1,sizeof(*program));CHECK(program);
    vm_init(&program->vm,&target,capture,&reply);program->vm.output=NULL;
    program->vm.memory=transition_inputs_lbc;program->vm.expected=sizeof(transition_inputs_lbc);
    CHECK(!vm_load(&program->vm));program->vm.ready=true;
    CHECK(!control_prepare(&program->vm)&&transition_program_capture(program));
    transition->effect=TRANSITION_PROGRAM;transition->programs[0]=program;transition->progress=50;
    App app={.send=capture,.transport=&reply,.source=1};AppOutput output={.transition=transition};
    AppOutput *outputs[]={&output};app.outputs=outputs;app.output_count=1;app.upload_output=&output;app.hardware.default_output=0;
    output.pixels.count=3;
    for(unsigned n=0;!program->vm.control.in_step&&n<1000;n++)transition_tick(transition,1);
    CHECK(transition->composing&&program->vm.control.in_step);
    const char *update="{\"op\":\"program_inputs\",\"target\":\"transition\",\"expected_generation\":1,\"expected_inputs_revision\":0,\"inputs\":{\"level\":{\"value\":0.8}}}";
    app_command(&app,CMD_MANAGE,1,(const uint8_t *)update,strlen(update));
    CHECK(reply.code==STATUS_OK&&transition->composing&&program->vm.control.in_step);
    for(unsigned n=0;transition->composing&&n<1000;n++)transition_tick(transition,1);
    CHECK(!transition->composing&&transition->prepared);
    for(unsigned n=0;n<3;n++)CHECK(transition->working[n*3]==20);
    transition_invalidate(transition);
    clock_now=transition->next_frame;
    transition_tick(transition,1);
    for(unsigned n=0;transition->composing&&n<1000;n++)transition_tick(transition,1);
    CHECK(!transition->composing&&transition->prepared);
    for(unsigned n=0;n<3;n++)CHECK(transition->working[n*3]==80);
    transition_destroy(transition);CHECK(vm_heap_used()==0);
}
