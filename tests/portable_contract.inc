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
    vm_init(vm,canvas,capture,reply);memcpy(vm->bytes,bytes,size);vm->expected=size;
    CHECK(!vm_load(vm));vm->ready=true;CHECK(!control_prepare(vm));
}
static uint32_t reference_word(uint32_t x){x^=x<<13;x^=x>>17;x^=x<<5;return x;}
typedef struct { uint32_t words[4];unsigned used; } ForcedWords;
static uint32_t forced_word(void *context){ForcedWords *words=context;CHECK(words->used<4);return words->words[words->used++];}
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

    // Freeze RNG v1 independently of the reference replay implementation.
    const uint32_t words[]={270369u,67634689u,2647435461u,307599695u,2398689233u};
    callback=(PortableBytes){rng_callback_lbc,sizeof(rng_callback_lbc)};
    CHECK(!pbc_open(&p,portable_read,&callback,callback.size));CHECK(!pbc_prepare(p,NULL));
    for(unsigned n=0;n<5;n++){
        CHECK(pbc_invoke(p,0,NULL,0));portable_finish(p,1);
        CHECK(p->resources[0].random_state==words[n]);
        CHECK(p->result.f==(words[n]>>8)*0x1p-24f);
    }
    pbc_destroy(p);CHECK(vm_heap_used()==0);

    ForcedWords forced={{UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX},0};int32_t rank=99;
    CHECK(!pbc_random_index(0,forced_word,&forced,&rank)&&forced.used==0);
    CHECK(pbc_random_index(65535,forced_word,&forced,&rank)&&rank==-1&&forced.used==4);
    forced.used=0;forced.words[3]=65534;
    CHECK(pbc_random_index(65535,forced_word,&forced,&rank)&&rank==65534&&forced.used==4);

    // Exercise the actual compiled Twinkle selector for every occupancy mask
    // and every free rank on strips of 1..10 pixels, using known RNG inputs.
    unsigned cases=0;uint8_t rgb[30];LbcBuffer output={rgb,1,1,false};
    PortableBytes selection={selection_lbc,sizeof(selection_lbc)};
    for(unsigned count=1;count<=10;count++)for(unsigned mask=0;mask<(1u<<count);mask++){
        unsigned free_pixels[10],free_count=0;
        for(unsigned n=0;n<count;n++)if(!(mask&(1u<<n)))free_pixels[free_count++]=n;
        for(unsigned desired=0;desired<(free_count?free_count:1);desired++){
            uint32_t seed=1;
            if(free_count)while(reference_word(seed)%free_count!=desired)seed++;
            CHECK(!pbc_open(&p,portable_read,&selection,selection.size));CHECK(!animation_validate(p));
            p->override_mask=3;p->overrides[0].u=seed;p->overrides[1].u=mask;output.count=count;
            CHECK(!pbc_prepare(p,&output));p->values[0].u=count;CHECK(pbc_invoke(p,p->entries[3],NULL,0));portable_finish(p,cases%2?1:256);
            CHECK(pbc_invoke(p,p->entries[2],NULL,0));portable_finish(p,7);
            CHECK(rgb[0]==(free_count?free_pixels[desired]+1:0));
            pbc_destroy(p);CHECK(vm_heap_used()==0);cases++;
        }
    }
    CHECK(cases==9227);

    uint8_t canvas_bytes[9]={0};PixelBuffer canvas={.rgb=canvas_bytes,.count=3};
    Capture reply={.marker=0x12345678u};static VmInstance vm;
    portable_load(&vm,&canvas,&reply,stamp_lbc,sizeof(stamp_lbc));p=vm.portable;
    // Birth differences remain precise beyond the f32 absolute-clock horizon.
    vm.control.program_us=UINT64_C(1)<<54;p->now_us=vm.control.program_us;
    CHECK(pbc_invoke(p,p->entries[3],NULL,0));portable_finish(p,1);
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
    vm_command(&vm,CMD_STOP,77,NULL,0);CHECK(!vm.playing&&!vm.control.in_step&&vm.control.program_us==20000&&p->ticks==2);
    vm_command(&vm,CMD_PLAY,77,NULL,0);CHECK(reply.code==STATUS_OK&&vm.control.committed[0]==(4000%256));
    CHECK(allocations==allocation_count);deny_allocation=false;
    // One 160 ms wall-credit pass executes four ticks and discards four intervals.
    bool ready=false;const char *error=animation_poll(&vm,160000,&ready);
    while(!error&&!ready)error=animation_poll(&vm,0,&ready);
    CHECK(!error&&p->ticks==6&&vm.control.program_us==100000&&p->dropped_ticks==4);
    vm_destroy(&vm);CHECK(vm_heap_used()==0);

    PixelBuffer physical={0};PixelConfig config={16,3,255,"RGB"};
    CHECK(pixels_init(&physical,&config,device_type("ws2812b")));
    portable_load(&vm,&physical,&reply,terminal_lbc,sizeof(terminal_lbc));
    vm_command(&vm,CMD_PLAY,77,NULL,0);CHECK(displayed[0]==1);
    deny_show=true;uint8_t delta[4];write_u32(delta,20);reply.snapshot_size=0;
    vm_command(&vm,CMD_FRAME,77,delta,sizeof(delta));
    CHECK(reply.code==STATUS_RUN&&vm.control.faulted&&!vm.portable->completed);
    CHECK(vm.portable->cycles==0&&vm.control.committed[0]==1&&displayed[0]==1);
    deny_show=false;vm_destroy(&vm);pixels_destroy(&physical);CHECK(vm_heap_used()==0);
}
