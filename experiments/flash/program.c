#include "program.h"
#include "config.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_partition.h"
static unsigned occupied;
bool program_map(ProgramMapping *map,const uint8_t *bytes,size_t size) {
    if (!size || size>MODULE_LIMIT || map->bytes) return false;
    const esp_partition_t *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"lightvm_code");
    if (!partition || partition->size<4u*32768u) return false;
    unsigned slot; for(slot=0;slot<4;slot++) if (!(occupied&(1u<<slot))) break;
    if(slot==4) return false;
    size_t offset=slot*32768u, erase=(size+4095u)&~4095u;
    if(esp_partition_erase_range(partition,offset,erase)!=ESP_OK ||
       esp_partition_write(partition,offset,bytes,size)!=ESP_OK) return false;
    const void *mapped; esp_partition_mmap_handle_t handle;
    if(esp_partition_mmap(partition,offset,size,ESP_PARTITION_MMAP_DATA,&mapped,&handle)!=ESP_OK) return false;
    if(memcmp(mapped,bytes,size)) { esp_partition_munmap(handle); return false; }
    occupied|=1u<<slot;
    *map=(ProgramMapping){mapped,size,(uintptr_t)handle,slot}; return true;
}
void program_unmap(ProgramMapping *map) {
    if(map->bytes) { esp_partition_munmap((esp_partition_mmap_handle_t)map->handle); occupied&=~(1u<<map->slot); }
    memset(map,0,sizeof(*map));
}
#else
#include <sys/mman.h>
#include <unistd.h>
bool program_map(ProgramMapping *map,const uint8_t *bytes,size_t size) {
    if (!size || size>MODULE_LIMIT || map->bytes) return false;
    size_t page=(size_t)sysconf(_SC_PAGESIZE), length=(size+page-1)/page*page;
    void *p=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(p==MAP_FAILED) return false;
    memcpy(p,bytes,size);
    if(mprotect(p,length,PROT_READ)) { munmap(p,length); return false; }
    *map=(ProgramMapping){p,size,length,0}; return true;
}
void program_unmap(ProgramMapping *map) {
    if(map->bytes) munmap((void *)map->bytes,map->handle);
    memset(map,0,sizeof(*map));
}
#endif
