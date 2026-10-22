#include "store_backend.h"
#include "vm.h"
#include "vm_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} } while(0)
#define SECTOR 4096u
#include "bytecode_fixtures.h"
typedef struct { uint8_t *bytes; uint32_t size; int failure; unsigned writes; bool deny_reads; } Flash;
static bool failing(Flash *f){f->writes++;if(f->failure==0)return true;if(f->failure>0)f->failure--;return false;}
static bool read_flash(void *context,uint32_t at,void *bytes,size_t size){
    Flash *f=context;CHECK(at<=f->size&&size<=f->size-at);if(f->deny_reads)return false;memcpy(bytes,f->bytes+at,size);return true;
}
static bool write_flash(void *context,uint32_t at,const void *bytes,size_t size){
    Flash *f=context;CHECK(at%256==0&&size%256==0&&at<=f->size&&size<=f->size-at);
    bool fail=failing(f);const uint8_t *data=bytes;size_t count=fail?size/2:size;
    for(size_t n=0;n<count;n++){CHECK((f->bytes[at+n]&data[n])==data[n]);f->bytes[at+n]=data[n];}return !fail;
}
static bool erase_flash(void *context,uint32_t at,size_t size){
    Flash *f=context;CHECK(at%SECTOR==0&&size%SECTOR==0&&at<=f->size&&size<=f->size-at);
    bool fail=failing(f);memset(f->bytes+at,255,fail?size/2:size);return !fail;
}
static bool sync_flash(void *context){return !failing(context);}
static Flash flash_new(uint32_t size){Flash f={.bytes=malloc(size),.size=size,.failure=-1};CHECK(f.bytes);memset(f.bytes,255,size);return f;}
static StoreBlockDevice device(Flash *f){return (StoreBlockDevice){.context=f,.capacity=f->size,.read=read_flash,.write=write_flash,.erase=erase_flash,.sync=sync_flash};}
static void mount_flash(Store *s,Flash *f){StoreBlockDevice d=device(f);CHECK(store_mount_with(s,&store_extent_backend,&d));}
static unsigned count(Store *s,StoreType type){uint32_t cursor=0;unsigned n=0;while(store_list(s,type,&cursor))n++;return n;}
static void expect(Store *s,const StoreEntry *entry,const char *text){char bytes[32]={0};CHECK(entry&&entry->size==strlen(text)&&store_read(s,entry,0,bytes,entry->size)&&!memcmp(bytes,text,entry->size));}
static void catalogues(void){
    for(unsigned trial=0;trial<2;trial++){
        Flash f=flash_new(trial?1024*1024:128*1024);Store s;mount_flash(&s,&f);
        CHECK(store_info(&s).total_bytes==f.size&&store_info(&s).free_bytes==f.size-2*SECTOR);
        unsigned objects=trial?90:16;
        for(unsigned n=0;n<objects;n++){char name[33];snprintf(name,sizeof(name),"p%u",n);CHECK(store_put(&s,STORE_ANIMATION,name,"animation",9));}
        CHECK(count(&s,STORE_ANIMATION)==objects);
        CHECK(store_put(&s,STORE_TRANSITION,"p0","transition",10));
        for(unsigned n=0;n<7;n++){char name[33];snprintf(name,sizeof(name),"recipe%u",n);CHECK(store_put(&s,STORE_PRESET,name,"{}",2));}
        CHECK(!store_find(&s,STORE_ANY,"p0"));
        expect(&s,store_find(&s,STORE_ANIMATION,"p0"),"animation");expect(&s,store_find(&s,STORE_TRANSITION,"p0"),"transition");
        CHECK(store_put(&s,STORE_ANIMATION,"p0","replacement",11));CHECK(count(&s,STORE_ANIMATION)==objects);
        uint32_t revision=s.generation;store_destroy(&s);mount_flash(&s,&f);
        CHECK(s.generation==revision&&count(&s,STORE_ANIMATION)==objects&&count(&s,STORE_PRESET)==7&&count(&s,STORE_TRANSITION)==1);
        CHECK(store_delete(&s,STORE_TRANSITION,"p0"));CHECK(store_find(&s,STORE_ANY,"p0"));
        store_destroy(&s);free(f.bytes);
    }
}
static void versions_and_uploads(void){
    Flash f=flash_new(128*1024);Store s;mount_flash(&s,&f);
    CHECK(store_put(&s,STORE_ANIMATION,"wipe","original",8));
    StoreEntry old=*store_find(&s,STORE_ANIMATION,"wipe");int pin=store_pin(&s,&old);CHECK(pin>=0);
    CHECK(store_put(&s,STORE_ANIMATION,"wipe","replacement",11));expect(&s,&old,"original");
    CHECK(store_delete(&s,STORE_ANIMATION,"wipe"));CHECK(!store_find(&s,STORE_ANIMATION,"wipe"));expect(&s,&old,"original");
    for(unsigned n=0;n<8;n++)CHECK(store_put(&s,STORE_TRANSITION,"another","something",9));
    expect(&s,&old,"original");uint32_t free_before=store_info(&s).free_bytes;store_unpin(&s,pin);CHECK(store_info(&s).free_bytes==free_before+SECTOR);
    uint8_t bytes[600];for(unsigned n=0;n<sizeof(bytes);n++)bytes[n]=(uint8_t)n;
    StoreUpload u={.pin=-1};free_before=store_info(&s).free_bytes;
    CHECK(store_upload_begin(&s,&u,STORE_ANIMATION,"stream",sizeof(bytes),crc32_bytes(bytes,sizeof(bytes))));
    CHECK(store_upload_chunk(&u,0,bytes,300));CHECK(store_upload_chunk(&u,0,bytes,300));
    CHECK(!store_upload_chunk(&u,0,"different",9));CHECK(!store_upload_chunk(&u,301,bytes,1));
    CHECK(!store_upload_finish(&u));CHECK(store_upload_chunk(&u,300,bytes+300,300));CHECK(store_upload_finish(&u));
    CHECK(!store_find(&s,STORE_ANIMATION,"stream"));CHECK(store_publish(&s,&u.entry));store_upload_cancel(&u);
    uint8_t readback[600];CHECK(store_read(&s,store_find(&s,STORE_ANIMATION,"stream"),0,readback,sizeof(readback))&&!memcmp(bytes,readback,sizeof(bytes)));
    CHECK(store_upload_begin(&s,&u,STORE_TRANSITION,"bad",sizeof(bytes),0));CHECK(store_upload_chunk(&u,0,bytes,sizeof(bytes)));CHECK(!store_upload_finish(&u));store_upload_cancel(&u);
    CHECK(!store_find(&s,STORE_TRANSITION,"bad"));CHECK(store_info(&s).free_bytes<free_before);
    store_destroy(&s);free(f.bytes);
}
static void failures(StoreType type){
    Flash f=flash_new(64*1024);Store s;mount_flash(&s,&f);CHECK(store_put(&s,type,"wipe","old",3));store_destroy(&s);
    uint8_t *baseline=malloc(f.size);CHECK(baseline);memcpy(baseline,f.bytes,f.size);unsigned failures=0;
    for(int n=0;n<35;n++){
        memcpy(f.bytes,baseline,f.size);mount_flash(&s,&f);f.failure=n;
        bool ok=store_put(&s,type,"wipe","new",3);f.failure=-1;store_destroy(&s);mount_flash(&s,&f);
        const StoreEntry *e=store_find(&s,type,"wipe");char bytes[3];CHECK(e&&store_read(&s,e,0,bytes,3));
        CHECK(!memcmp(bytes,"old",3)||!memcmp(bytes,"new",3));if(ok)CHECK(!memcmp(bytes,"new",3));else failures++;
        store_destroy(&s);
    }
    CHECK(failures>8&&failures<35);
    memcpy(f.bytes,baseline,f.size);mount_flash(&s,&f);
    unsigned saved=0;for(;;){char name[33];snprintf(name,sizeof(name),"full%u",saved);if(!store_put(&s,type,name,"fill",4))break;saved++;CHECK(saved<32);}
    CHECK(saved>8);expect(&s,store_find(&s,type,"wipe"),"old");
    store_destroy(&s);mount_flash(&s,&f);CHECK(count(&s,type)==saved+1);store_destroy(&s);
    f.deny_reads=true;StoreBlockDevice d=device(&f);CHECK(!store_mount_with(&s,&store_extent_backend,&d)&&!s.available);store_destroy(&s);
    free(baseline);free(f.bytes);
}
static void cancellable_writes(void){
    Flash f=flash_new(128*1024);Store s;mount_flash(&s,&f);
    CHECK(store_put(&s,STORE_PRESET,"scene","old",3));
    unsigned boundary=0;
    for(;;boundary++){
        StoreWrite write={0};uint32_t generation=s.generation,revision=s.preset_revision;
        uint32_t free_before=store_info(&s).free_bytes;
        CHECK(store_write_begin(&s,&write,STORE_PRESET,"scene","new",3));
        CHECK(!store_put(&s,STORE_PRESET,"other","x",1));
        CHECK(!store_delete(&s,STORE_PRESET,"scene"));
        StoreUpload upload={.pin=-1};CHECK(!store_upload_begin(&s,&upload,STORE_ANIMATION,"x",1,0));
        int status=0;
        for(unsigned n=0;n<boundary&&!status;n++){
            unsigned writes=f.writes;status=store_write_step(&write);
            CHECK(f.writes-writes<=2); /* final marker write + sync only */
            if(!status){CHECK(s.generation==generation&&s.preset_revision==revision);expect(&s,store_find(&s,STORE_PRESET,"scene"),"old");}
        }
        CHECK(status>=0);store_write_cancel(&write);CHECK(!s.writer);
        if(status){CHECK(s.generation==generation+1&&s.preset_revision==revision+1);break;}
        CHECK(store_info(&s).free_bytes==free_before);
        store_destroy(&s);mount_flash(&s,&f);expect(&s,store_find(&s,STORE_PRESET,"scene"),"old");
        CHECK(boundary<200);
    }
    CHECK(boundary>20);expect(&s,store_find(&s,STORE_PRESET,"scene"),"new");
    store_destroy(&s);mount_flash(&s,&f);expect(&s,store_find(&s,STORE_PRESET,"scene"),"new");store_destroy(&s);free(f.bytes);
}
static void readonly_assets(void){
    StoreAsset assets[]={{STORE_ANIMATION,"wipe",slot_lbc,sizeof(slot_lbc)},{STORE_TRANSITION,"wipe","transition",10}};
    StoreAssets configuration={assets,2};Store s;CHECK(store_mount_with(&s,&store_assets_backend,&configuration));
    CHECK(!store_info(&s).writable&&store_info(&s).free_bytes==0&&count(&s,STORE_ANY)==2);
    CHECK(!store_find(&s,STORE_ANY,"wipe"));CHECK(!store_put(&s,STORE_ANIMATION,"new","x",1));CHECK(!store_delete(&s,STORE_ANIMATION,"wipe"));
    const StoreEntry *e=store_find(&s,STORE_ANIMATION,"wipe");VmInstance vm;PixelBuffer output={.count=5};vm_init(&vm,&output,NULL,NULL);
    CHECK(vm_bind_storage(&vm,&s,e));CHECK(!vm_load(&vm));vm.ready=true;CHECK(!control_prepare(&vm));CHECK(!control_step(&vm,5000));vm_destroy(&vm);
    CHECK(vm_heap_used()==0);store_destroy(&s);
    assets[1].type=STORE_ANIMATION;CHECK(!store_mount_with(&s,&store_assets_backend,&configuration));store_destroy(&s);
}
int main(void){cancellable_writes();catalogues();versions_and_uploads();failures(STORE_ANIMATION);failures(STORE_OUTPUT_STATE);failures(STORE_KEY_FEEDBACK);readonly_assets();puts("storage backends passed");return 0;}
