#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include "config.h"
#include "port.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

static const PlatformInfo info={"host",LIGHTVM_BOARD_NAME,"memory"};
const PlatformInfo *platform_info(void) { return &info; }
uint64_t platform_time_us(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*1000000u+(uint64_t)now.tv_nsec/1000u;
}
#define PIN(n) {"GP" #n,n}
static const BoardPin pins[]={PIN(0),PIN(1),PIN(2),PIN(3),PIN(4),PIN(5),PIN(6),PIN(7),PIN(8),PIN(9),PIN(10),PIN(11),PIN(12),PIN(13),PIN(14),PIN(15),PIN(16),PIN(17),PIN(18),PIN(19),PIN(20),PIN(21),PIN(22),PIN(26),PIN(27),PIN(28)};
const BoardPin *platform_pins(size_t *count) { *count=sizeof(pins)/sizeof(pins[0]); return pins; }
bool platform_pixels_open(const PixelConfig *config,void **driver) {
    PixelConfig *p=malloc(sizeof(*p)); if (!p) return false;
    *p=*config; *driver=p; return true;
}
void platform_pixels_close(void *driver) { free(driver); }
bool platform_pixels_show(void *driver,const uint8_t *rgb,size_t count) {
    (void)rgb; return driver && count==((PixelConfig *)driver)->count;
}
static int storage_fd=-1;
#if LIGHTVM_BOOT_SEQUENCE
static int boot_storage_fd=-1;
#endif
static uint32_t storage_identity;
void platform_identity(char *device,size_t capacity,char *boot,size_t boot_capacity) {
    char hostname[128]="localhost"; gethostname(hostname,sizeof(hostname)-1);
    snprintf(device,capacity,"host-%08lx-%08lx",(unsigned long)crc32_bytes((const uint8_t *)hostname,strlen(hostname)),(unsigned long)(storage_fd>=0?storage_identity:(uint32_t)getpid()));
    snprintf(boot,boot_capacity,"%lx-%llx",(unsigned long)getpid(),(unsigned long long)platform_time_us());
}
static bool file_bytes(int fd,uint32_t capacity,bool write,uint32_t offset,void *bytes,size_t size) {
    if (fd<0 || offset>capacity || size>capacity-offset) return false;
    uint8_t *p=bytes;
    while (size) {
        ssize_t n=write ? pwrite(fd,p,size,offset) : pread(fd,p,size,offset);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return false;
        p+=n; offset+=(uint32_t)n; size-=(size_t)n;
    }
    return true;
}
static bool io_bytes(bool write,uint32_t offset,void *bytes,size_t size) {
    return file_bytes(storage_fd,STORAGE_BYTES,write,offset,bytes,size);
}
#if LIGHTVM_BOOT_SEQUENCE
static void boot_storage_open(const char *path) {
    static const char suffix[]=".boot-sequence";
    size_t length=strlen(path);
    char *name=malloc(length+sizeof(suffix));
    if (!name) return;
    memcpy(name,path,length); memcpy(name+length,suffix,sizeof(suffix));
    int fd=open(name,O_RDWR|O_CREAT,0600); free(name);
    if (fd<0) return;
    struct stat st;
    struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET};
    if (fcntl(fd,F_SETLK,&lock) || fstat(fd,&st)) { close(fd); return; }
    if (st.st_size==0) {
        uint8_t erased[4096]; memset(erased,255,sizeof(erased));
        for (uint32_t offset=0;offset<BOOT_SEQUENCE_STORAGE_BYTES;offset+=sizeof(erased)) {
            if (!file_bytes(fd,BOOT_SEQUENCE_STORAGE_BYTES,true,offset,erased,sizeof(erased))) { close(fd); return; }
        }
        if (fsync(fd)) { close(fd); return; }
    } else if (st.st_size!=BOOT_SEQUENCE_STORAGE_BYTES) { close(fd); return; }
    boot_storage_fd=fd;
}
bool platform_boot_storage_available(void) { return boot_storage_fd>=0; }
bool platform_boot_storage_read(uint32_t offset,void *bytes,size_t size) {
    return file_bytes(boot_storage_fd,BOOT_SEQUENCE_STORAGE_BYTES,false,offset,bytes,size);
}
bool platform_boot_storage_write(uint32_t offset,const void *bytes,size_t size) {
    return offset%256==0 && size%256==0 && file_bytes(boot_storage_fd,BOOT_SEQUENCE_STORAGE_BYTES,true,offset,(void *)bytes,size);
}
bool platform_boot_storage_erase(uint32_t offset,size_t size) {
    if (offset%4096 || size%4096 || offset>BOOT_SEQUENCE_STORAGE_BYTES || size>BOOT_SEQUENCE_STORAGE_BYTES-offset) return false;
    uint8_t erased[4096]; memset(erased,255,sizeof(erased));
    while (size) {
        if (!platform_boot_storage_write(offset,erased,sizeof(erased))) return false;
        offset+=sizeof(erased); size-=sizeof(erased);
    }
    return true;
}
bool platform_boot_storage_sync(void) { return boot_storage_fd>=0 && fsync(boot_storage_fd)==0; }
#endif
bool host_storage_open(const char *path) {
    storage_identity=crc32_bytes((const uint8_t *)path,strlen(path));
    storage_fd=open(path,O_RDWR|O_CREAT,0600);
    if (storage_fd<0) return false;
    struct stat st;
    struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET};
    if (fcntl(storage_fd,F_SETLK,&lock) || fstat(storage_fd,&st)) { host_storage_close(); return false; }
    if (st.st_size==0) {
        uint8_t erased[4096]; memset(erased,255,sizeof(erased));
        for (uint32_t offset=0;offset<STORAGE_BYTES;offset+=sizeof(erased))
            if (!io_bytes(true,offset,erased,sizeof(erased))) { host_storage_close(); return false; }
        if (fsync(storage_fd)) { host_storage_close(); return false; }
    } else if (st.st_size!=STORAGE_BYTES) { host_storage_close(); return false; }
#if LIGHTVM_BOOT_SEQUENCE
    boot_storage_open(path);
#endif
    return true;
}
void host_storage_close(void) {
    if (storage_fd>=0) close(storage_fd);
    storage_fd=-1;
#if LIGHTVM_BOOT_SEQUENCE
    if (boot_storage_fd>=0) close(boot_storage_fd);
    boot_storage_fd=-1;
#endif
}
uint32_t platform_storage_capacity(void) { return STORAGE_BYTES; }
bool platform_storage_available(void) {
    if(storage_fd>=0)return true;
    char path[]="/tmp/lightvm-flash-XXXXXX";storage_fd=mkstemp(path);
    if(storage_fd<0)return false;
    unlink(path);uint8_t erased[4096];memset(erased,255,sizeof(erased));
    for(uint32_t at=0;at<STORAGE_BYTES;at+=sizeof(erased))if(!io_bytes(true,at,erased,sizeof(erased))){host_storage_close();return false;}
    return true;
}
bool platform_storage_read(uint32_t offset,void *bytes,size_t size) { return io_bytes(false,offset,bytes,size); }
bool platform_storage_write(uint32_t offset,const void *bytes,size_t size) {
    if (offset%256 || size%256) return false;
    return io_bytes(true,offset,(void *)bytes,size);
}
bool platform_storage_erase(uint32_t offset,size_t size) {
    if (offset%4096 || size%4096 || offset>STORAGE_BYTES || size>STORAGE_BYTES-offset) return false;
    uint8_t erased[4096]; memset(erased,255,sizeof(erased));
    while (size) { if (!io_bytes(true,offset,erased,sizeof(erased))) return false; offset+=sizeof(erased); size-=sizeof(erased); }
    return true;
}
bool platform_storage_sync(void) { return storage_fd>=0 && fsync(storage_fd)==0; }
void *platform_calloc(size_t count,size_t size) { return calloc(count,size); }
void *platform_realloc(void *memory,size_t size) { return realloc(memory,size); }
void platform_free(void *memory) { free(memory); }
