/* Assert the bytes borrowed by the native driver, not the unscaled snapshot. */
static void light_controls_contract(void){
    clock_now=0;clock_step=0;storage_available=false;deny_allocation=false;deny_show=false;
    CHECK(vm_heap_used()==0);
    Capture reply={.marker=0x12345678u};
    AppOutput output={0};AppOutput *outputs[]={&output};
    App app={.outputs=outputs,.output_count=1,.send=capture,.transport=&reply};
    app.hardware.default_output=0;app.upload_output=&output;strcpy(output.hardware.id,"lights");
    PixelConfig config={.pin=16,.count=3,.brightness=7,.order="RGB",.clock_pin=-1,.enable_pin=-1};
    CHECK(pixels_init(&output.pixels,&config,output_type("ws2812b")));
    portable_load(&output.vm,&output.pixels,&reply,render_lbc,sizeof(render_lbc));
    output.vm.output=&output.pixels;
    uint8_t delta[4]={0};
    app_command(&app,CMD_FRAME,42,delta,sizeof(delta));
    CHECK(reply.code==STATUS_OK&&displayed[0]==255&&displayed[1]==128);
    CHECK(app_output_power(&output)); /* A manually presented frame stays on. */
    uint64_t time=output.vm.control.program_us;
    uint32_t generation=output.vm.generation,frames=output.vm.control.contribution_frame;
    const char *set="{\"op\":\"output_light_set\",\"expected_revision\":0,\"brightness\":0.5}";
    app_command(&app,CMD_MANAGE,42,(const uint8_t *)set,strlen(set));
    CHECK(reply.code==STATUS_OK&&displayed[0]==128&&displayed[1]==64);
    CHECK(output.pixels.rgb[0]==255&&output.pixels.rgb[1]==128);
    CHECK(output.vm.control.program_us==time&&output.vm.control.contribution_frame==frames);
    CHECK(output.vm.generation==generation&&app_output_power(&output));
    CHECK(((PixelConfig *)output.pixels.driver)->brightness==7); /* Hardware limit is separate. */
    /* A brightness edit cancels a borrowed frame before overwriting its buffer. */
    delayed_output=true;output_ready=false;output_failed=false;
    output.vm.playing=true;clock_now=20000;
    for(unsigned n=0;n<100&&!output.vm.output_pending;n++)vm_tick(&output.vm);
    CHECK(output.vm.output_pending&&borrowed_output==output.pixels.dimmed);
    unsigned cancelled=cancelled_outputs;
    set="{\"op\":\"output_light_set\",\"expected_revision\":1,\"brightness\":0.25}";
    app_command(&app,CMD_MANAGE,42,(const uint8_t *)set,strlen(set));
    CHECK(reply.code==STATUS_OK&&!borrowed_output&&cancelled_outputs==cancelled+1);
    CHECK(displayed[0]==64&&displayed[1]==32&&output.pixels.rgb[0]==255);
    app_command(&app,CMD_STOP,42,NULL,0);CHECK(!app_output_power(&output)&&displayed[0]==0);
    vm_destroy(&output.vm);delayed_output=false;

    /* Layers composite at full precision; the final dimmer runs once. */
    Composition *s=composition_create(&output.pixels,2);CHECK(s);
    for(unsigned n=0;n<2;n++)portable_load(&s->layers[n].vm,&s->layers[n].target,&reply,render_lbc,sizeof(render_lbc));
    s->layers[0].opacity=128;s->layers[1].opacity=128;
    output.composition=s;CHECK(!composition_frame(s,0));
    CHECK(output.pixels.rgb[0]==192&&displayed[0]==48);
    CHECK(s->layers[0].vm.control.committed[0]==255);
    composition_destroy(s);output.composition=NULL;

    /* Both A/B endpoints and the mix are unscaled until physical presentation. */
    Transition *t=transition_create(&output.pixels);CHECK(t);output.transition=t;
    for(unsigned n=0;n<2;n++){
        t->compositions[n]=composition_create(&t->target,0);CHECK(t->compositions[n]);
        memset(t->compositions[n]->front,n?200:100,9);
        t->compositions[n]->playing=false;
    }
    CHECK(transition_set(t,50));
    for(unsigned n=0;n<20&&!t->frames;n++)transition_tick(t,64);
    CHECK(t->frames&&output.pixels.rgb[0]==150&&displayed[0]==38);
    CHECK(t->compositions[0]->front[0]==100&&t->compositions[1]->front[0]==200);
    output.power_on=true;transition_play(t,false);frames=t->frames;
    set="{\"op\":\"output_light_set\",\"expected_revision\":2,\"brightness\":0.5}";
    app_command(&app,CMD_MANAGE,42,(const uint8_t *)set,strlen(set));
    CHECK(reply.code==STATUS_OK&&displayed[0]==75&&t->frames==frames);
    CHECK(output.pixels.rgb[0]==150&&app_output_power(&output));
    for(unsigned n=0;n<2;n++)composition_destroy(t->compositions[n]);
    transition_destroy(t);output.transition=NULL;pixels_destroy(&output.pixels);
    CHECK(vm_heap_used()==0);
}
