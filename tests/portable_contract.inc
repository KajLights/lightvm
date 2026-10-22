/* Included by the native test port; uses its clock, allocation and show faults. */
#include "animation.h"
#include "portable_fixtures.h"
typedef struct { const uint8_t *bytes; size_t size; } PortableBytes;
static unsigned manifest_reads,package_reads;
static bool portable_read(void *context,uint32_t offset,void *bytes,size_t size){
    PortableBytes *source=context;if(offset>source->size||size>source->size-offset)return false;
    package_reads++;
    if(source->size>=96&&size==lbc_u32(source->bytes+76)&&offset==source->size-size)manifest_reads++;
    memcpy(bytes,source->bytes+offset,size);return true;
}
static void portable_finish(Portable *p,unsigned quantum){
    unsigned calls=0;int status;
    do{status=pbc_resume(p,quantum);CHECK(++calls<100000);}while(!status);
    CHECK(status==1);
}
static void portable_load(VmInstance *vm,PixelBuffer *canvas,Capture *reply,const uint8_t *bytes,size_t size){
    vm_init(vm,canvas,capture,reply);vm->memory=bytes;vm->expected=size;
    CHECK(!vm_load(vm));vm->ready=true;CHECK(!control_prepare(vm));
}
static void portable_contract(void){
    /* All exponent classes, signs, zeros, subnormals and NaN payload classes. */
    const uint32_t fractions[]={0,1,0x3fffff,0x400000,0x7fffff};
    for(unsigned sign=0;sign<2;sign++)for(unsigned exponent=0;exponent<256;exponent++)
        for(unsigned n=0;n<sizeof(fractions)/sizeof(fractions[0]);n++){
            uint32_t bits=(sign<<31)|(exponent<<23)|fractions[n];float value;
            memcpy(&value,&bits,sizeof(value));CHECK(lbc_finite(value)==(exponent!=255));
        }
    clock_now=0;clock_step=0;storage_available=false;deny_allocation=false;deny_show=false;
    CHECK(vm_heap_used()==0);
    PortableBytes metadata={library_lbc,sizeof(library_lbc)};Portable *loaded=NULL;manifest_reads=0;
    CHECK(!pbc_open(&loaded,portable_read,&metadata,metadata.size));CHECK(manifest_reads==1);
    unsigned reads_before=package_reads;size_t heap_before=vm_heap_used();
    for(unsigned repeat=0;repeat<20;repeat++)for(unsigned r=0;r<loaded->nregs;r++){
        uint32_t expected=lbc_u32(metadata.bytes+96+r*8+4);
        CHECK(pbc_initial(loaded,r)==expected);
        if(pbc_role(loaded,r)==R_CONST)CHECK(pbc_value(loaded,r).u==expected);
    }
    CHECK(package_reads==reads_before&&vm_heap_used()==heap_before);
    pbc_destroy(loaded);CHECK(vm_heap_used()==0);
    // Generic typed entries are not required to have any animation lifecycle.
    PortableBytes callback={callback_lbc,sizeof(callback_lbc)};Portable *p=NULL;
    CHECK(!pbc_open(&p,portable_read,&callback,callback.size));CHECK(animation_validate(p));
    PbcValue argument={.i=21};CHECK(!pbc_prepare(p,NULL));
    CHECK(!pbc_invoke(p,0,&argument,0));CHECK(pbc_invoke(p,0,&argument,1));portable_finish(p,1);
    CHECK(p->result.i==42);
    CHECK(pbc_invoke(p,0,&argument,1));p->used=LIGHTVM_INVOCATION_LIMIT;
    CHECK(pbc_resume(p,1)==-1); /* A new slice cannot reset the invocation limit. */
    pbc_destroy(p);CHECK(vm_heap_used()==0);
    callback=(PortableBytes){array_callback_lbc,sizeof(array_callback_lbc)};
    CHECK(!pbc_open(&p,portable_read,&callback,callback.size));CHECK(!pbc_prepare(p,NULL));
    CHECK(pbc_invoke(p,0,NULL,0));portable_finish(p,1);CHECK(p->result.i==42);
    pbc_destroy(p);CHECK(vm_heap_used()==0);

    // Bulk pixel maps resume with a one-dispatch quota, without allocations.
    uint8_t map_bytes[67*3]={0};PixelBuffer map_canvas={.rgb=map_bytes,.count=67};
    Capture map_reply={0};static VmInstance map_vm;
    portable_load(&map_vm,&map_canvas,&map_reply,pixel_map_lbc,sizeof(pixel_map_lbc));
    p=map_vm.portable;unsigned map_allocations=allocations;
    uint64_t map_dispatches=p->dispatch_count;
    PbcValue map_args[3]={{.i=67},{.f=1},{.f=1}};
    deny_allocation=true;CHECK(pbc_invoke(p,p->entries[1],map_args,3));portable_finish(p,1);deny_allocation=false;
    CHECK(allocations==map_allocations&&p->dispatch_count-map_dispatches<32);
    for(unsigned i=0;i<67;i++)CHECK(p->resources[0].data[i*4]==i&&p->resources[0].data[i*4+1]==0&&p->resources[0].data[i*4+3]==255);
    vm_destroy(&map_vm);CHECK(vm_heap_used()==0);

    uint8_t canvas_bytes[9]={0};PixelBuffer canvas={.rgb=canvas_bytes,.count=3};
    Capture reply={.marker=0x12345678u};static VmInstance vm;
    portable_load(&vm,&canvas,&reply,delta_lbc,sizeof(delta_lbc));p=vm.portable;
    // Delta remains precise even when absolute f32 time loses milliseconds.
    vm.control.program_us=UINT64_C(1)<<54;
    CHECK(!control_step(&vm,12345)&&vm.control.committed[0]==12);
    vm.view.generation++;CHECK(control_step(&vm,0)&&vm.control.faulted);vm.view.generation--;
    vm_destroy(&vm);CHECK(vm_heap_used()==0);

    portable_load(&vm,&canvas,&reply,slow_lbc,sizeof(slow_lbc));p=vm.portable;
    uint8_t first=vm.control.committed[0];unsigned allocation_count=allocations;
    deny_allocation=true;vm.playing=true;vm.last_frame=0;vm.next_frame=20000;clock_now=20000;
    vm_tick(&vm);CHECK(vm.control.in_step&&vm.control.program_us==0&&vm.control.committed[0]==first);
    /* Publication may make a skipped VM runnable after collection. It must
       wait for the next collector, never start a serial fallback job. */
    uint64_t dispatches=p->dispatch_count;
    vm_tick_begin();vm_tick_budget(&vm,4096);CHECK(p->dispatch_count==dispatches);
    CHECK(vm_tick_collect(&vm,1));CHECK(!vm_tick_collect(&vm,1));vm_tick_flush();
    CHECK(p->dispatch_count==dispatches+1);vm_tick_budget(&vm,4096);
    CHECK(p->dispatch_count==dispatches+1);vm_tick_end();
    unsigned pc=p->pc;static const char snapshot[]="{\"op\":\"program_snapshot\"}";
    Json snapshot_json;CHECK(json_parse(&snapshot_json,(const uint8_t*)snapshot,strlen(snapshot)));
    CHECK(control_manage(&vm,77,&snapshot_json,"program_snapshot"));CHECK(p->pc==pc&&reply.code==STATUS_OK);
    // STOP finishes a partially executed update, then clears; no partial render.
    vm_command(&vm,CMD_STOP,77,NULL,0);CHECK(!vm.playing&&!vm.control.in_step&&vm.control.program_us==20000&&p->render_count==2);
    vm_command(&vm,CMD_PLAY,77,NULL,0);CHECK(reply.code==STATUS_OK&&vm.control.committed[0]==(4000%256));
    CHECK(allocations==allocation_count);deny_allocation=false;
    // One delayed call renders once with the entire elapsed delta.
    bool ready=false;const char *error=animation_poll(&vm,160000,&ready);
    while(!error&&!ready)error=animation_poll(&vm,0,&ready);
    CHECK(!error&&p->render_count==3&&vm.control.program_us==180000);
    vm_destroy(&vm);CHECK(vm_heap_used()==0);

    PixelBuffer physical={0};PixelConfig config={.pin=16,.count=3,.brightness=255,.order="RGB",.clock_pin=-1,.enable_pin=-1};
    CHECK(pixels_init(&physical,&config,output_type("ws2812b")));
    portable_load(&vm,&physical,&reply,counter_lbc,sizeof(counter_lbc));
    // A passive device snapshot remains RGB before the first presentation.
    static const char physical_snapshot[]="{\"op\":\"program_snapshot\"}";
    reply.snapshot_size=0;
    CHECK(json_parse(&snapshot_json,(const uint8_t*)physical_snapshot,strlen(physical_snapshot)));
    CHECK(control_manage(&vm,77,&snapshot_json,"program_snapshot"));
    CHECK(reply.code==STATUS_OK&&reply.snapshot_size==9);
    CHECK(!memcmp(reply.snapshot,physical.rgb,9)&&!vm.control.output_valid);
    vm_command(&vm,CMD_PLAY,77,NULL,0);CHECK(displayed[0]==1);
    deny_show=true;uint8_t delta[4];write_u32(delta,20);reply.snapshot_size=0;
    vm_command(&vm,CMD_FRAME,77,delta,sizeof(delta));
    CHECK(reply.code==STATUS_EXECUTION&&!vm.control.faulted);
    CHECK(vm.portable->render_count==2&&vm.control.committed[0]==2&&displayed[0]==1);
    deny_show=false;vm_destroy(&vm);pixels_destroy(&physical);CHECK(vm_heap_used()==0);

    // Native output owns the exact frame until completion/cancellation. A
    // renderer must keep running after the driver releases the frame.
    CHECK(pixels_init(&physical,&config,output_type("ws2812b")));
    portable_load(&vm,&physical,&reply,counter_lbc,sizeof(counter_lbc));
    vm_command(&vm,CMD_PLAY,77,NULL,0);CHECK(reply.code==STATUS_OK);
    delayed_output=true;output_ready=false;output_failed=false;clock_now+=20000;
    uint32_t output_frame=vm.control.output_frame;vm_tick(&vm);
    CHECK(vm.output_pending&&!vm.view.sealed&&borrowed_output==physical.dimmed);
    CHECK(vm.portable->render_count==2);
    CHECK(vm.control.output_frame==output_frame&&displayed[0]==1);
    uint64_t ticks=vm.portable->render_count;
    for(unsigned n=0;n<5;n++){
        clock_now+=20000;vm_tick(&vm);
        CHECK(vm.portable->render_count==ticks+1&&vm.frame_ready);
        CHECK(crc32_bytes(borrowed_output,borrowed_count*3)==borrowed_crc);
    }
    output_ready=true;vm_tick(&vm);
    CHECK(!vm.output_pending&&!vm.view.sealed&&!borrowed_output&&vm.portable->render_count==3);
    CHECK(vm.control.output_frame>=output_frame+1&&vm.playing);
    ticks=vm.portable->render_count;output_frame=vm.control.output_frame;
    for(unsigned n=0;n<25;n++){clock_now+=20000;vm_tick(&vm);}
    CHECK(vm.playing&&!vm.control.faulted&&vm.portable->render_count>ticks);
    CHECK(vm.control.output_frame>output_frame&&displayed[0]>2);
    vm_destroy(&vm);
    portable_load(&vm,&physical,&reply,counter_lbc,sizeof(counter_lbc));
    vm_command(&vm,CMD_PLAY,77,NULL,0);output_ready=false;clock_now+=20000;vm_tick(&vm);
    CHECK(vm.output_pending);unsigned cancelled=cancelled_outputs;
    vm_command(&vm,CMD_STOP,77,NULL,0);
    CHECK(cancelled_outputs==cancelled+1&&!borrowed_output&&!vm.output_pending&&displayed[0]==0);
    vm_destroy(&vm);
    portable_load(&vm,&physical,&reply,counter_lbc,sizeof(counter_lbc));
    vm_command(&vm,CMD_PLAY,77,NULL,0);clock_now+=20000;vm_tick(&vm);CHECK(vm.output_pending);
    output_failed=true;vm_tick(&vm);
    CHECK(!vm.control.faulted&&!borrowed_output&&vm.playing);
    delayed_output=false;output_failed=false;vm_destroy(&vm);pixels_destroy(&physical);CHECK(vm_heap_used()==0);
}
