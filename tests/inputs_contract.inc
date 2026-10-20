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
    vm_destroy(&vm);input_registry_destroy(&registry);CHECK(vm_heap_used()==0);

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
