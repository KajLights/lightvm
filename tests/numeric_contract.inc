/* Uses the native test port's allocation faults and clock. The scalar fixture
 * below writes wire bytes independently of the Python compiler. */
#include <fenv.h>
#include "draw.h"
typedef struct { const char *name; unsigned op,left,right,result; } NumericCase;
static const NumericCase numeric_cases[]={
    {"q16.add",40,7,7,7},{"q16.sub",41,7,7,7},{"q16.mul",42,7,7,7},
    {"q16.div",43,7,7,7},{"q16.mod",44,7,7,7},{"q16.neg",45,7,0,7},
    {"q16.min",46,7,7,7},{"q16.max",47,7,7,7},{"q16.lt",48,7,7,4},
    {"q16.eq",17,7,7,4},{"q16.from_i32",49,1,0,7},{"q16.to_i32",50,7,0,1},
    {"q16.from_f32",51,2,0,7},{"q16.to_f32",52,7,0,2},
    {"q16.from_bits",53,1,0,7},{"q16.bits",54,7,0,1},{"q16.sqrt",55,7,0,7},
    {"i32.and",56,1,1,1},{"i32.or",57,1,1,1},{"i32.xor",58,1,1,1},
    {"i32.not",59,1,0,1},{"i32.shl",60,1,1,1},{"i32.shr",61,1,1,1},
    {"f32.abs",62,2,0,2},{"f32.floor",63,2,0,2},{"f32.ceil",64,2,0,2},
    {"f32.trunc",65,2,0,2},{"f32.sin",66,2,0,2},{"f32.cos",67,2,0,2},
    {"f32.sqrt",68,2,0,2},{"f32.log",69,2,0,2},{"f32.exp",70,2,0,2},
    {"f32.pow",71,2,2,2},{"f32.min",25,2,2,2},{"f32.max",26,2,2,2}
};
static size_t numeric_package(uint8_t bytes[256],const NumericCase *test){
    static const char manifest[]="{\"requires\":\"0.1.0\"}";
    memset(bytes,0,256);memcpy(bytes,"LBC1",4);
    write_u32(bytes+4,4);write_u32(bytes+8,3);write_u32(bytes+12,2);
    write_u32(bytes+20,1);write_u32(bytes+28,1);write_u32(bytes+76,sizeof(manifest)-1);
    bytes[96]=(uint8_t)test->left;bytes[97]=1;
    bytes[104]=(uint8_t)(test->right?test->right:1);bytes[105]=(uint8_t)(test->right?1:0);
    bytes[112]=(uint8_t)test->result;
    write_u32(bytes+116,test->result==4?0:0x5a5aa5a5);
    bytes[120]=(uint8_t)test->op;bytes[121]=2;bytes[122]=0;bytes[123]=(uint8_t)(test->right?1:0);
    bytes[128]=1;bytes[129]=2; /* RETURN result */
    write_u32(bytes+140,2);write_u32(bytes+148,test->result);
    write_u32(bytes+156,test->right?2:1);bytes[161]=1;
    memcpy(bytes+176,manifest,sizeof(manifest)-1);
    return 176+sizeof(manifest)-1;
}
static int numeric_metadata_driver(void){
    puts("[");
    for(unsigned n=0;n<sizeof(numeric_cases)/sizeof(*numeric_cases);n++){
        const NumericCase *test=&numeric_cases[n];LbcNumericMetadata m=lbc_numeric_metadata(test->op);
        CHECK(m.signature.left==test->left&&m.signature.right==test->right&&m.signature.result==test->result);
        CHECK(lbc_instruction_work(test->op)==m.work);
        /* Exercise the loader's real signatures, not only the generated table. */
        uint8_t bytes[256];PortableBytes source={bytes,numeric_package(bytes,test)};Portable *p=NULL;
        CHECK(!pbc_open(&p,portable_read,&source,source.size));pbc_destroy(p);CHECK(vm_heap_used()==0);
        const unsigned types[]={1,2,3,4,7};
        for(unsigned k=0;k<5;k++)if(types[k]!=test->left){
            bytes[96]=(uint8_t)types[k];p=NULL;
            CHECK(pbc_open(&p,portable_read,&source,source.size));CHECK(!p&&vm_heap_used()==0);
        }
        bytes[96]=(uint8_t)test->left;
        if(test->right)for(unsigned k=0;k<5;k++)if(types[k]!=test->right){
            bytes[104]=(uint8_t)types[k];p=NULL;
            CHECK(pbc_open(&p,portable_read,&source,source.size));CHECK(!p&&vm_heap_used()==0);
        }
        bytes[104]=(uint8_t)(test->right?test->right:1);
        for(unsigned k=0;k<5;k++)if(types[k]!=test->result){
            bytes[112]=(uint8_t)types[k];write_u32(bytes+116,0);write_u32(bytes+148,types[k]);p=NULL;
            CHECK(pbc_open(&p,portable_read,&source,source.size));CHECK(!p&&vm_heap_used()==0);
        }
        numeric_package(bytes,test);
        char *floor=strstr((char*)bytes+176,"0.1.0");CHECK(floor);
        memcpy(floor,"0.0.9",5);p=NULL;
        CHECK(pbc_open(&p,portable_read,&source,source.size));CHECK(!p&&vm_heap_used()==0);
        printf("%s{\"name\":\"%s\",\"id\":%u,\"signature\":[%u,%u%s",n?",":"",test->name,test->op,m.signature.result,m.signature.left,m.signature.right?",":"");
        if(m.signature.right)printf("%u",m.signature.right);
        printf("],\"release\":\"%s\",\"kernel_release\":\"%s\",\"work\":%u,\"effects\":%u,\"kernel\":%s}\n",LIGHTVM_BASELINE,LIGHTVM_BASELINE,m.work,m.effects,m.kernel?"true":"false");
    }
    LbcNumericMetadata sine=lbc_native_numeric_metadata(7),instruction=lbc_numeric_metadata(FSIN);
    CHECK(!memcmp(&sine,&instruction,sizeof(sine)));
    CHECK(lbc_native_instruction_work(7)==sine.work&&lbc_native_instruction_work(8)==1);
    puts("]");return 0;
}
static int numeric_driver(const char *rounding){
    int mode=!strcmp(rounding,"nearest")?FE_TONEAREST:!strcmp(rounding,"down")?FE_DOWNWARD:
        !strcmp(rounding,"up")?FE_UPWARD:!strcmp(rounding,"zero")?FE_TOWARDZERO:-1;
    CHECK(mode!=-1&&!fesetround(mode));
    char name[40];unsigned left,right;
    while(scanf("%39s %x %x",name,&left,&right)==3){
        const NumericCase *test=NULL;
        for(unsigned n=0;n<sizeof(numeric_cases)/sizeof(*numeric_cases);n++)
            if(!strcmp(name,numeric_cases[n].name)){test=&numeric_cases[n];break;}
        CHECK(test);uint8_t bytes[256];PortableBytes source={bytes,numeric_package(bytes,test)};
        Portable *p=NULL;CHECK(!pbc_open(&p,portable_read,&source,source.size));
        CHECK(!pbc_prepare(p,NULL));PbcValue arguments[2]={{.u=left},{.u=right}};
        unsigned before=allocations;deny_allocation=true;
        int status=-1;
        if(pbc_invoke(p,0,arguments,test->right?2:1)){
            do{status=pbc_resume(p,1);}while(!status);
        }
        deny_allocation=false;CHECK(allocations==before);
        printf("%u %08x\n",status==1,pbc_value(p,2).u);
        pbc_destroy(p);CHECK(vm_heap_used()==0);
    }
    return ferror(stdin)?1:0;
}
static int numeric_package_driver(const char *path,unsigned count,unsigned tick,unsigned budget,bool stats,unsigned frames){
    static uint8_t bytes[32769],pixels[1024*4];CHECK(count>=1&&count<=1024);
    FILE *file=fopen(path,"rb");CHECK(file);
    size_t size=fread(bytes,1,sizeof(bytes),file);CHECK(!ferror(file));fclose(file);
    PortableBytes source={bytes,size};Portable *p=NULL;
    const char *error=pbc_open(&p,portable_read,&source,size);
    if(error){puts("{\"load\":false}");CHECK(vm_heap_used()==0);return 0;}
    p->deterministic_random=true;p->default_seed=0x5eed;
    LbcBuffer output={.data=pixels,.count=count,.generation=1};
    error=pbc_prepare(p,p->profile==3?&output:NULL);
    if(error){puts("{\"load\":true,\"prepare\":false}");pbc_destroy(p);CHECK(vm_heap_used()==0);return 0;}
    PbcValue args[3]={{.i=(int32_t)count},{.f=1.25f},{.f=0.25f}};clock_step=tick;
    unsigned before=allocations;deny_allocation=true;
    int status=1;
    for(unsigned invocation=0;invocation<(p->profile==3?1u+frames:1u)&&status==1;invocation++){
        unsigned fn=invocation?1:0;
        if(invocation>1)args[1].f+=args[2].f;
        p->now_us=(uint64_t)(args[1].f*1000000.0f);
        if(!pbc_invoke(p,fn,p->profile==3?args:NULL,p->profile==3?3:0))status=-1;
        else {if(stats&&fn==(p->profile==3?1u:0u))p->used=LIGHTVM_INVOCATION_LIMIT-budget;do{status=pbc_resume(p,1);}while(!status);}
    }
    deny_allocation=false;CHECK(allocations==before);
    printf("{\"load\":true,\"prepare\":true,\"status\":%d,\"state\":[",status);
    bool comma=false;
    for(unsigned r=0;r<p->nregs;r++)if(pbc_role(p,r)==R_STATE){
        printf("%s%u",comma?",":"",pbc_value(p,r).u);comma=true;
    }
    printf("],\"arrays\":[");comma=false;
    for(unsigned n=0;n<p->nresources;n++)if(p->resources[n].kind==20){
        printf("%s[",comma?",":"");comma=true;PbcResource *r=&p->resources[n];
        for(unsigned k=0;k<r->count;k++){uint32_t value;memcpy(&value,r->data+k*4,4);printf("%s%u",k?",":"",value);}
        printf("]");
    }
    printf("],\"pixels\":\"");for(unsigned n=0;n<count*4;n++)printf("%02x",pixels[n]);
    printf("\",\"plan_bytes\":%zu,\"images\":[",p->map_bytes);comma=false;
    for(unsigned n=0;n<p->nresources;n++)if(p->resources[n].kind>=14&&p->resources[n].kind<=17&&!p->resources[n].sealed){
        PbcResource *r=&p->resources[n];printf("%s\"",comma?",":"");comma=true;
        for(unsigned k=0;k<r->count*draw_stride(r->kind);k++){
            printf("%02x",r->data[k]);
        }
        printf("\"");
    }
    printf("]");if(stats)printf(",\"work\":%u,\"dispatches\":%llu",p->used-(LIGHTVM_INVOCATION_LIMIT-budget),(unsigned long long)p->dispatch_count);
    puts("}");pbc_destroy(p);CHECK(vm_heap_used()==0);return 0;
}
static int noise_snapshot_driver(const char *path){
    uint8_t bytes[32769],pixels[129*4]={0},baseline[129*4];
    FILE *file=fopen(path,"rb");CHECK(file);
    size_t size=fread(bytes,1,sizeof(bytes),file);CHECK(!ferror(file));fclose(file);
    PortableBytes source={bytes,size};Portable *p=NULL;
    CHECK(!pbc_open(&p,portable_read,&source,size));
    LbcBuffer output={.data=pixels,.count=129,.generation=1};
    CHECK(!pbc_prepare(p,&output));CHECK(p->ninputs==2);
    PbcValue args[3]={{.i=129},{.f=1.25f},{.f=0.25f}};
    CHECK(pbc_invoke(p,0,args,3));portable_finish(p,1);
    unsigned before=allocations;deny_allocation=true;clock_step=1000;
    for(unsigned frame=0;frame<3;frame++){
        inputs_capture(p,0,false);CHECK(pbc_invoke(p,1,args,3));
        int status=pbc_resume(p,1);CHECK(status==0);
        if(frame==1){
            /* Updating drivers during a yielded invocation must not mutate its
             * immutable snapshot. The next invocation selects the new field. */
            p->inputs[0].driver.value.f=1.75f;p->inputs[1].driver.value.i=7;
        }
        do{status=pbc_resume(p,1);}while(!status);CHECK(status==1);
        if(frame==0)memcpy(baseline,pixels,sizeof(pixels));
        else CHECK((memcmp(baseline,pixels,sizeof(pixels))==0)==(frame==1));
    }
    deny_allocation=false;CHECK(allocations==before);pbc_destroy(p);CHECK(!vm_heap_used());return 0;
}
static void numeric_contract(void){
    clock_now=clock_step=0;CHECK(vm_heap_used()==0);
    /* Every allocation failure and every incremental loader/preparer return
       must be cancellable, including a partially filled fixed array. */
    PortableBytes source={fixed_array_lbc,sizeof(fixed_array_lbc)};
    uint8_t pixels[67*4]={0};LbcBuffer output={.data=pixels,.count=67,.generation=1};
    Portable *p=NULL;
    for(unsigned fixture=0;fixture<2;fixture++){
    source=fixture?(PortableBytes){extended_map_lbc,sizeof(extended_map_lbc)}:(PortableBytes){fixed_array_lbc,sizeof(fixed_array_lbc)};
    unsigned start=allocations;
    CHECK(!pbc_open(&p,portable_read,&source,source.size));CHECK(!pbc_prepare(p,&output));
    unsigned allocation_count=allocations-start;pbc_destroy(p);CHECK(vm_heap_used()==0);
    for(unsigned fail=1;fail<=allocation_count;fail++){
        fail_allocation_at=allocations+fail;
        const char *error=pbc_open(&p,portable_read,&source,source.size);
        if(!error)error=pbc_prepare(p,&output);
        CHECK(error);pbc_destroy(p);CHECK(vm_heap_used()==0);fail_allocation_at=0;
    }
    for(unsigned cancel=0;cancel<1000;cancel++){
        PbcLoader loader={0};PbcPreparation prep={0};p=NULL;int loaded=0,prepared=0;
        for(unsigned step=0;step<cancel&&!prepared;step++){
            if(!loaded){loaded=pbc_open_step(&loader,&p,portable_read,&source,source.size);CHECK(loaded>=0);}
            else {prepared=pbc_prepare_step(&prep,p,&output);CHECK(prepared>=0);}
        }
        pbc_open_cancel(&loader);pbc_destroy(p);CHECK(vm_heap_used()==0);
        if(prepared)break;
        CHECK(cancel<999);
    }
    CHECK(!pbc_open(&p,portable_read,&source,source.size));CHECK(!pbc_prepare(p,&output));
    if(fixture){
        CHECK(p->map_bytes>0);PbcValue args[3]={{.i=67},{.f=1.25f},{.f=0.25f}};
        unsigned before=allocations;deny_allocation=true;
        CHECK(pbc_invoke(p,0,args,3));portable_finish(p,1);
        CHECK(pbc_invoke(p,1,args,3));portable_finish(p,1);
        CHECK(allocations==before);deny_allocation=false;
        pbc_destroy(p);CHECK(vm_heap_used()==0);continue;
    }
    CHECK(p->payload_bytes==2u*67u*4u); /* Four bytes per admitted element. */
    PbcValue args[3]={{.i=67},{.f=1.25f},{.f=0.25f}};
    unsigned before=allocations;deny_allocation=true;
    CHECK(pbc_invoke(p,0,args,3));portable_finish(p,1);
    CHECK(pbc_invoke(p,1,args,3));portable_finish(p,1);
    CHECK(allocations==before);deny_allocation=false;
    for(unsigned k=0;k<67;k++){
        uint32_t a,b;memcpy(&a,p->resources[1].data+4*k,4);memcpy(&b,p->resources[2].data+4*k,4);
        CHECK(a==163840u&&b==163840u);
    }
    CHECK(pbc_invoke(p,1,args,3));CHECK(pbc_resume(p,1)==0);
    p->resources[1].generation++;CHECK(pbc_resume(p,1)==-1);
    pbc_destroy(p);CHECK(vm_heap_used()==0);
    }
}
