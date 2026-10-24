#include "app.h"
#include "platform.h"
#include "release.h"
static bool busy(void *p){return ((App *)p)->command_active;}
static void dispatch(void *p,uint64_t source,SendMessage send,void *transport,uint8_t type,uint16_t id,const uint8_t *bytes,size_t size){app_dispatch(p,source,send,transport,type,id,bytes,size);}
static void disconnect(void *p,uint64_t source){app_disconnect(p,source);}
static void delivered(void *p,uint64_t source){app_reply_delivered(p,source);}
static enum Status save(void *p,const NetworkConfig *c,uint32_t revision,uint64_t source,char *error,size_t cap){return app_save_network(p,c,revision,source,error,cap);}
DeviceHost app_device_host(App *a){
    return (DeviceHost){.context=a,.controller_id=a->controller_id,.boot_id=a->boot_id,
        .product="LightVM",.board=platform_info()->board,.version=LIGHTVM_VERSION,
        .kind="light_controller",.hostname_prefix="lightvm",.capabilities=DEVICE_CAP_CONFIGURATION|DEVICE_CAP_LIGHT_OUTPUT,
        .connection_sequence=&a->connection_sequence,.network=&a->network,.settings=&a->network_settings,
        .busy=busy,.dispatch=dispatch,.disconnect=disconnect,.reply_delivered=delivered,.save_network=save};
}
