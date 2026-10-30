#include "device/metadata.h"
#include "device/platform.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

bool device_metadata_entry_valid(const StoreEntry *e){
    return e->type==STORE_DEVICE_METADATA && !strcmp(e->name,STORE_DEVICE) && e->size && e->size<=DEVICE_METADATA_BYTES;
}
uint32_t device_metadata_revision(const DeviceMetadata *m){return m->store->type_revision[STORE_DEVICE_METADATA];}
static bool nickname(const Json *j,int t,char out[DEVICE_NICKNAME_BYTES+1]){
    if(t>=0 && j->tokens[t].type==JSON_PRIMITIVE && j->tokens[t].end-j->tokens[t].start==4 && !memcmp(j->text+j->tokens[t].start,"null",4)){out[0]=0;return true;}
    char raw[FRAME_LIMIT+1];if(!json_string(j,t,raw,sizeof(raw)))return false;
    for(const unsigned char *p=(const unsigned char *)raw;*p;p++)if(*p<32||*p==127)return false;
    size_t n=strlen(raw),first=0;while(first<n&&raw[first]==' ')first++;
    while(n>first&&raw[n-1]==' ')n--;
    if(n-first>DEVICE_NICKNAME_BYTES)return false;
    memcpy(out,raw+first,n-first);out[n-first]=0;return true;
}
static size_t encoded(const char *name,char out[2*DEVICE_NICKNAME_BYTES+3]){
    size_t n=0;out[n++]='"';
    for(const char *p=name;*p;p++){if(*p=='"'||*p=='\\')out[n++]='\\';out[n++]=*p;}
    out[n++]='"';out[n]=0;return n;
}
size_t device_metadata_json(const DeviceMetadata *m,char *out,size_t capacity){
    char escaped[2*DEVICE_NICKNAME_BYTES+3];encoded(m->nickname,escaped);
    int n=snprintf(out,capacity,"{\"schema\":\"kajlights.device_metadata\",\"nickname\":%s,\"revision\":%u}",m->nickname[0]?escaped:"null",(unsigned)device_metadata_revision(m));
    return n>=0&&(size_t)n<capacity?(size_t)n:0;
}
void device_metadata_init(DeviceMetadata *m,Store *store){
    memset(m,0,sizeof(*m));m->store=store;
    const StoreEntry *entry=store_find(store,STORE_DEVICE_METADATA,STORE_DEVICE);if(!entry)return;
    char bytes[DEVICE_METADATA_BYTES];Json *j=platform_calloc(1,sizeof(*j));
    static const char *const fields[]={"schema","nickname",NULL};
    bool ok=j&&entry->size<=sizeof(bytes)&&store_read(store,entry,0,bytes,entry->size)&&json_parse(j,bytes,entry->size)&&json_fields(j,0,fields)&&json_equal(j,json_get(j,0,"schema"),"kajlights.device_metadata")&&nickname(j,json_get(j,0,"nickname"),m->nickname);
    platform_free(j);if(!ok){m->nickname[0]=0;snprintf(m->error,sizeof(m->error),"device metadata invalid; save a new name or reprovision");}
}
static enum Status candidate(const DeviceMetadata *m,const Json *j,const char *boot_id,char name[DEVICE_NICKNAME_BYTES+1],char *error,size_t capacity){
    static const char *const fields[]={"op","nickname","expected_revision","expected_boot_id",NULL};
    uint32_t revision;
    if(!json_fields(j,0,fields)||!json_u32(j,json_get(j,0,"expected_revision"),&revision)||!json_equal(j,json_get(j,0,"expected_boot_id"),boot_id)||revision!=device_metadata_revision(m)||revision==UINT32_MAX){snprintf(error,capacity,"device metadata boot/revision conflict");return STATUS_CONFIG;}
    if(!nickname(j,json_get(j,0,"nickname"),name)){snprintf(error,capacity,"nickname must be null or valid UTF-8 up to 63 bytes without control characters");return STATUS_CONFIG;}
    if(!m->store->available||!store_info(m->store).writable){snprintf(error,capacity,"device metadata storage unavailable or read-only");return STATUS_STORAGE;}
    return STATUS_OK;
}
enum Status device_metadata_validate(const DeviceMetadata *m,const Json *j,const char *boot_id,char *error,size_t capacity){
    char name[DEVICE_NICKNAME_BYTES+1];return candidate(m,j,boot_id,name,error,capacity);
}
enum Status device_metadata_set(DeviceMetadata *m,const Json *j,const char *boot_id,char *error,size_t capacity){
    char name[DEVICE_NICKNAME_BYTES+1];enum Status status=candidate(m,j,boot_id,name,error,capacity);if(status!=STATUS_OK)return status;
    char escaped[2*DEVICE_NICKNAME_BYTES+3],data[DEVICE_METADATA_BYTES];encoded(name,escaped);
    int n=snprintf(data,sizeof(data),"{\"schema\":\"kajlights.device_metadata\",\"nickname\":%s}",name[0]?escaped:"null");
    if(n<0||(size_t)n>=sizeof(data)||!store_put(m->store,STORE_DEVICE_METADATA,STORE_DEVICE,data,(uint32_t)n)){snprintf(error,capacity,"device metadata save failed; name retained");return STATUS_STORAGE;}
    memcpy(m->nickname,name,strlen(name)+1);m->error[0]=0;return STATUS_OK;
}
