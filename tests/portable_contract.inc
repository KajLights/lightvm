/* Included by the native test port; uses its clock, allocation and show faults. */
#include "animation.h"
#include "portable_fixtures.h"
typedef struct { const uint8_t *bytes; size_t size; } PortableBytes;
static bool portable_read(void *context,uint32_t offset,void *bytes,size_t size){
    PortableBytes *source=context;if(offset>source->size||size>source->size-offset)return false;
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
    clock_now=0;clock_step=0;storage_available=false;deny_allocation=false;deny_show=false;
    CHECK(vm_heap_used()==0);
    // Generic typed entries are not required to have any animation lifecycle.
    PortableBytes callback={callback_lbc,sizeof(callback_lbc)};Portable *p=NULL;
    CHECK(!pbc_open(&p,portable_read,&callback,callback.size));CHECK(animation_validate(p));
    PbcValue argument={.i=21};CHECK(!pbc_prepare(p,NULL));
    CHECK(!pbc_invoke(p,0,&argument,0));CHECK(pbc_invoke(p,0,&argument,1));portable_finish(p,1);
    CHECK(p->result.i==42);pbc_destroy(p);CHECK(vm_heap_used()==0);
    callback=(PortableBytes){array_callback_lbc,sizeof(array_callback_lbc)};
    CHECK(!pbc_open(&p,portable_read,&callback,callback.size));CHECK(!pbc_prepare(p,NULL));
    CHECK(pbc_invoke(p,0,NULL,0));portable_finish(p,1);CHECK(p->result.i==42);
    pbc_destroy(p);CHECK(vm_heap_used()==0);

    uint8_t canvas_bytes[9]={0};PixelBuffer canvas={.rgb=canvas_bytes,.count=3};
    Capture reply={.marker=0x12345678u};static VmInstance vm;
    portable_load(&vm,&canvas,&reply,delta_lbc,sizeof(delta_lbc));p=vm.portable;
    // Delta remains precise even when absolute f32 time loses milliseconds.
    vm.control.program_us=UINT64_C(1)<<54;
    CHECK(!control_step(&vm,1,12345)&&vm.control.committed[0]==12);
    vm.view.generation++;CHECK(control_step(&vm,1,0)&&vm.control.faulted);vm.view.generation--;
    vm_destroy(&vm);CHECK(vm_heap_used()==0);

    portable_load(&vm,&canvas,&reply,slow_lbc,sizeof(slow_lbc));p=vm.portable;
    uint8_t first=vm.control.committed[0];unsigned allocation_count=allocations;
    deny_allocation=true;vm.playing=true;vm.last_frame=0;vm.next_frame=20000;clock_now=20000;
    vm_tick(&vm);CHECK(vm.control.in_step&&vm.control.program_us==0&&vm.control.committed[0]==first);
    unsigned pc=p->pc;static const char snapshot[]="{\"op\":\"program_snapshot\"}";
    CHECK(control_manage(&vm,77,(const uint8_t*)snapshot,strlen(snapshot)));CHECK(p->pc==pc&&reply.code==STATUS_OK);
    // STOP finishes a partially executed update, then clears; no partial render.
    vm_command(&vm,CMD_STOP,77,NULL,0);CHECK(!vm.playing&&!vm.control.in_step&&vm.control.program_us==20000&&p->render_count==2);
    vm_command(&vm,CMD_PLAY,77,NULL,0);CHECK(reply.code==STATUS_OK&&vm.control.committed[0]==(4000%256));
    CHECK(allocations==allocation_count);deny_allocation=false;
    // One delayed call renders once with the entire elapsed delta.
    bool ready=false;const char *error=animation_poll(&vm,160000,&ready);
    while(!error&&!ready)error=animation_poll(&vm,0,&ready);
    CHECK(!error&&p->render_count==3&&vm.control.program_us==180000);
    vm_destroy(&vm);CHECK(vm_heap_used()==0);

    PixelBuffer physical={0};PixelConfig config={16,3,255,"RGB"};
    CHECK(pixels_init(&physical,&config,device_type("ws2812b")));
    portable_load(&vm,&physical,&reply,counter_lbc,sizeof(counter_lbc));
    vm_command(&vm,CMD_PLAY,77,NULL,0);CHECK(displayed[0]==1);
    deny_show=true;uint8_t delta[4];write_u32(delta,20);reply.snapshot_size=0;
    vm_command(&vm,CMD_FRAME,77,delta,sizeof(delta));
    CHECK(reply.code==STATUS_RUN&&!vm.control.faulted);
    CHECK(vm.portable->render_count==2&&vm.control.committed[0]==2&&displayed[0]==1);
    deny_show=false;vm_destroy(&vm);pixels_destroy(&physical);CHECK(vm_heap_used()==0);

    // Native output owns the exact frame until completion/cancellation. A
    // renderer must keep running after the driver releases the frame.
    CHECK(pixels_init(&physical,&config,device_type("ws2812b")));
    portable_load(&vm,&physical,&reply,counter_lbc,sizeof(counter_lbc));
    vm_command(&vm,CMD_PLAY,77,NULL,0);CHECK(reply.code==STATUS_OK);
    delayed_output=true;output_ready=false;output_failed=false;clock_now+=20000;
    uint32_t output_frame=vm.control.output_frame;vm_tick(&vm);
    CHECK(vm.output_pending&&vm.view.sealed&&borrowed_output==control_frame(&vm));
    CHECK(vm.portable->render_count==2);
    CHECK(vm.control.output_frame==output_frame&&displayed[0]==1);
    uint64_t ticks=vm.portable->render_count;
    for(unsigned n=0;n<5;n++){clock_now+=20000;vm_tick(&vm);CHECK(vm.portable->render_count==ticks);}
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
