#include "platform.h"
#include "apa102.h"
#include "runtime.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "apa102.pio.h"
#include <stdlib.h>

typedef struct { unsigned references;int32_t sda,scl;i2c_inst_t *instance; } Bus;
static Bus buses[2];
bool platform_i2c_valid(int32_t sda,int32_t scl) {
    return sda>=0&&scl>=0&&sda!=scl&&(sda&1)==0&&(scl&1)==1&&((sda>>1)&1)==((scl>>1)&1);
}
bool platform_i2c_open(int32_t sda,int32_t scl,void **driver) {
    if(!platform_i2c_valid(sda,scl))return false;
    unsigned index=(unsigned)(sda>>1)&1;Bus *b=&buses[index];
    if(b->references&&(b->sda!=sda||b->scl!=scl))return false;
    if(!b->references){
        b->sda=sda;b->scl=scl;b->instance=index?i2c1:i2c0;i2c_init(b->instance,400000);
        gpio_set_function((uint)sda,GPIO_FUNC_I2C);gpio_set_function((uint)scl,GPIO_FUNC_I2C);
        gpio_pull_up((uint)sda);gpio_pull_up((uint)scl);
    }
    b->references++;*driver=b;return true;
}
void platform_i2c_close(void *driver) {
    Bus *b=driver;if(!b||!b->references)return;
    if(!--b->references){i2c_deinit(b->instance);gpio_init((uint)b->sda);gpio_init((uint)b->scl);}
}
bool platform_i2c_transfer(void *driver,uint8_t address,const uint8_t *write,size_t wn,uint8_t *read,size_t rn) {
    Bus *b=driver;if(!b||!b->references||!wn)return false;
    absolute_time_t deadline=make_timeout_time_us(1000);
    int n=i2c_write_blocking_until(b->instance,address,write,wn,rn>0,deadline);
    if(n!=(int)wn)return false;
    return !rn||i2c_read_blocking_until(b->instance,address,read,rn,false,deadline)==(int)rn;
}

typedef struct {
    PixelConfig config;
    PIO pio;
    uint sm,offset;
    int dma;
    uint8_t *wire;
    size_t size;
    uint64_t deadline,drain;
    bool pending;
} ClockedPixels;
bool platform_apa102_supported(void) { return true; }
static void enable(ClockedPixels *p,bool active) {
    if(p->config.enable_pin>=0)gpio_put((uint)p->config.enable_pin,!active);
}
bool platform_apa102_open(const PixelConfig *c,void **driver) {
    ClockedPixels *p=calloc(1,sizeof(*p));if(!p)return false;
    p->config=*c;p->size=apa102_frame_size(c->count);p->dma=-1;
    p->wire=malloc(p->size);if(p->wire)p->dma=dma_claim_unused_channel(false);
    if(p->dma<0){free(p->wire);free(p);return false;}
    const PIO blocks[]={pio0,pio1};int sm=-1;
    for(unsigned n=0;n<2;n++){
        p->pio=blocks[n];if(!pio_can_add_program(p->pio,&apa102_program))continue;
        sm=pio_claim_unused_sm(p->pio,false);if(sm>=0)break;
    }
    if(sm<0){dma_channel_unclaim((uint)p->dma);free(p->wire);free(p);return false;}
    p->sm=(uint)sm;p->offset=pio_add_program(p->pio,&apa102_program);
    if(c->enable_pin>=0){gpio_init((uint)c->enable_pin);gpio_put((uint)c->enable_pin,1);gpio_set_dir((uint)c->enable_pin,GPIO_OUT);}
    apa102_program_init(p->pio,p->sm,p->offset,(uint)c->pin,(uint)c->clock_pin,(float)clock_get_hz(clk_sys)/(2*c->frequency));
    dma_channel_config d=dma_channel_get_default_config((uint)p->dma);
    channel_config_set_transfer_data_size(&d,DMA_SIZE_8);channel_config_set_read_increment(&d,true);channel_config_set_write_increment(&d,false);
    channel_config_set_dreq(&d,pio_get_dreq(p->pio,p->sm,true));
    dma_channel_configure((uint)p->dma,&d,&p->pio->txf[p->sm],NULL,0,false);*driver=p;return true;
}
static int clocked_fault(ClockedPixels *p) {
    pio_sm_set_enabled(p->pio,p->sm,false);dma_channel_abort((uint)p->dma);enable(p,false);
    dma_channel_hw_addr((uint)p->dma)->al1_ctrl|=DMA_CH0_CTRL_TRIG_READ_ERROR_BITS|DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    pio_sm_clear_fifos(p->pio,p->sm);pio_sm_restart(p->pio,p->sm);
    pio_sm_exec(p->pio,p->sm,pio_encode_jmp(p->offset));pio_sm_set_enabled(p->pio,p->sm,true);
    p->pending=false;p->drain=0;return -1;
}
int platform_apa102_poll(void *driver) {
    ClockedPixels *p=driver;if(!p)return -1;if(!p->pending)return 1;
    uint32_t flags=dma_channel_hw_addr((uint)p->dma)->ctrl_trig;
    if(flags&(DMA_CH0_CTRL_TRIG_READ_ERROR_BITS|DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS))return clocked_fault(p);
    uint64_t now=time_us_64();
    if(dma_channel_is_busy((uint)p->dma)||!pio_sm_is_tx_fifo_empty(p->pio,p->sm))return now>=p->deadline?clocked_fault(p):0;
    // FIFO drain does not include the last byte in the shift register.
    if(!p->drain){p->drain=now+(8000000u+p->config.frequency-1)/p->config.frequency+2;return 0;}
    if(now<p->drain)return 0;
    enable(p,false);p->pending=false;p->drain=0;return 1;
}
void platform_apa102_cancel(void *driver) { if(driver)while(!platform_apa102_poll(driver))tight_loop_contents(); }
bool platform_apa102_submit(void *driver,const uint8_t *rgb,size_t count) {
    ClockedPixels *p=driver;if(!p||p->pending||!apa102_encode(&p->config,rgb,count,p->wire,p->size))return false;
    p->drain=0;p->pending=true;p->deadline=time_us_64()+p->size*8000000ull/p->config.frequency+100000;
    enable(p,true);dma_channel_transfer_from_buffer_now((uint)p->dma,p->wire,(uint32_t)p->size);return true;
}
bool platform_apa102_show(void *driver,const uint8_t *rgb,size_t count) {
    platform_apa102_cancel(driver);if(!platform_apa102_submit(driver,rgb,count))return false;
    int status;do{status=platform_apa102_poll(driver);if(!status)runtime_checkpoint();}while(!status);return status>0;
}
void platform_apa102_close(void *driver) {
    ClockedPixels *p=driver;if(!p)return;platform_apa102_cancel(p);enable(p,false);
    dma_channel_cleanup((uint)p->dma);dma_channel_unclaim((uint)p->dma);
    pio_sm_set_enabled(p->pio,p->sm,false);pio_sm_unclaim(p->pio,p->sm);pio_remove_program(p->pio,&apa102_program,p->offset);
    gpio_init((uint)p->config.pin);gpio_init((uint)p->config.clock_pin);
    if(p->config.enable_pin>=0)gpio_init((uint)p->config.enable_pin);
    free(p->wire);free(p);
}
