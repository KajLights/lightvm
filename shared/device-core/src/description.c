#include "device/description.h"
#include "device/metadata.h"
#include <stdio.h>
void device_description(const DeviceHost *host,const char *limits,DeviceDescriptionWrite write,void *ctx){
    write(ctx,"\"product\":");device_json_write_string(write,ctx,host->product);
    write(ctx,",\"kind\":");device_json_write_string(write,ctx,host->kind);
    write(ctx,",\"controller_id\":");device_json_write_string(write,ctx,host->controller_id);
    write(ctx,",\"boot_id\":");device_json_write_string(write,ctx,host->boot_id);
    write(ctx,",\"board\":");device_json_write_string(write,ctx,host->board);
    write(ctx,",\"version\":");device_json_write_string(write,ctx,host->version);
    char fields[96];snprintf(fields,sizeof(fields),",\"capabilities\":%u,\"metadata_revision\":%u,\"nickname\":",(unsigned)host->capabilities,host->metadata?(unsigned)device_metadata_revision(host->metadata):0u);write(ctx,fields);
    if(host->metadata&&host->metadata->nickname[0])device_json_write_string(write,ctx,host->metadata->nickname);else write(ctx,"null");
    write(ctx,",\"metadata_error\":");device_json_write_string(write,ctx,host->metadata?host->metadata->error:"");
    write(ctx,",\"firmware\":");write(ctx,device_firmware_json);
    write(ctx,",\"features\":");write(ctx,device_features_json);
    write(ctx,",\"hardware\":");write(ctx,device_hardware_json);
    write(ctx,",\"updates\":");write(ctx,device_updates_json);
    write(ctx,",\"limits\":");write(ctx,limits);
}
