#include "platform.h"
#include "config.h"
#include "runtime.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "ws2812.pio.h"
#include <stdlib.h>

typedef struct {
    PIO pio;
    uint sm,offset;
    int dma;
    PixelConfig config;
    uint32_t *words;
    uint64_t latch_until,deadline;
    bool pending;
} Pixels;

bool platform_pixels_open(const PixelConfig *config,void **driver) {
    if(!config||!driver||!config->count||config->count>PIXEL_CAPACITY)return false;
    Pixels *p=calloc(1,sizeof(*p));
    if(!p)return false;
    p->config=*config;p->dma=-1;
    p->words=calloc(config->count,sizeof(*p->words));
    if(p->words)p->dma=dma_claim_unused_channel(false);
    if(p->dma<0){free(p->words);free(p);return false;}
    int sm=-1;
    const PIO blocks[]={pio0,pio1};
    for(unsigned n=0;n<2;n++){
        p->pio=blocks[n];
        if(!pio_can_add_program(p->pio,&ws2812_program))continue;
        sm=pio_claim_unused_sm(p->pio,false);if(sm>=0)break;
    }
    if(sm<0){dma_channel_unclaim((uint)p->dma);free(p->words);free(p);return false;}
    p->sm=(uint)sm;p->offset=pio_add_program(p->pio,&ws2812_program);
    ws2812_program_init(p->pio,p->sm,p->offset,(uint)config->pin,800000,false);
    dma_channel_config c=dma_channel_get_default_config((uint)p->dma);
    channel_config_set_transfer_data_size(&c,DMA_SIZE_32);
    channel_config_set_read_increment(&c,true);
    channel_config_set_write_increment(&c,false);
    channel_config_set_dreq(&c,pio_get_dreq(p->pio,p->sm,true));
    dma_channel_configure((uint)p->dma,&c,&p->pio->txf[p->sm],NULL,0,false);
    *driver=p;return true;
}

static int output_fault(Pixels *p) {
    /* Stop all SRAM references before resetting or releasing a failed frame.
     * No completion IRQ is installed; SDK abort waits for in-flight DMA writes. */
    pio_sm_set_enabled(p->pio,p->sm,false);
    dma_channel_abort((uint)p->dma);
    // The non-triggering alias clears W1C errors without restarting the
    // aborted channel's remaining transfer count.
    dma_channel_hw_addr((uint)p->dma)->al1_ctrl|=
        DMA_CH0_CTRL_TRIG_READ_ERROR_BITS|DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    pio_sm_clear_fifos(p->pio,p->sm);pio_sm_restart(p->pio,p->sm);
    pio_sm_exec(p->pio,p->sm,pio_encode_jmp(p->offset));
    sleep_us(330); /* Discard a partial frame and establish reset-low before retry. */
    pio_sm_set_enabled(p->pio,p->sm,true);
    p->pending=false;p->latch_until=0;return -1;
}

int platform_pixels_poll(void *driver) {
    Pixels *p=driver;
    if(!p)return -1;
    if(!p->pending)return 1;
    uint32_t flags=dma_channel_hw_addr((uint)p->dma)->ctrl_trig;
    if(flags&(DMA_CH0_CTRL_TRIG_READ_ERROR_BITS|DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS))return output_fault(p);
    uint64_t now=time_us_64();
    if(dma_channel_is_busy((uint)p->dma)||!pio_sm_is_tx_fifo_empty(p->pio,p->sm))
        return now>=p->deadline?output_fault(p):0;
    /* DMA completion only means the FIFO has been fed. Even an empty FIFO can
     * leave a 30-us pixel in the shift register; allow that plus 300 us reset.
     * Observe drain before arming this guard, including after a delayed poll. */
    if(!p->latch_until){p->latch_until=now+330;return 0;}
    if(now<p->latch_until)return 0;
    p->pending=false;p->latch_until=0;return 1;
}

void platform_pixels_cancel(void *driver) {
    Pixels *p=driver;
    if(!p)return;
    /* Finish a normal frame; a stalled transfer takes the bounded fault path.
     * Either outcome releases DMA's references before this function returns. */
    while(!platform_pixels_poll(p))tight_loop_contents();
}

bool platform_pixels_submit(void *driver,const uint8_t *rgb,size_t count) {
    Pixels *p=driver;
    if(!p||!rgb||count!=p->config.count||p->pending)return false;
    for(size_t i=0;i<count;i++){
        uint32_t word=0;
        for(size_t c=0;c<3;c++){
            unsigned channel=p->config.order[c]=='R'?0:p->config.order[c]=='G'?1:2;
            word=(word<<8)|((rgb[i*3+channel]*p->config.brightness+127u)/255u);
        }
        p->words[i]=word<<8;
    }
    p->pending=true;p->latch_until=0;p->deadline=time_us_64()+count*30u+100000u;
    dma_channel_transfer_from_buffer_now((uint)p->dma,p->words,(uint32_t)count);
    return true;
}

bool platform_pixels_show(void *driver,const uint8_t *rgb,size_t count) {
    platform_pixels_cancel(driver);
    if(!platform_pixels_submit(driver,rgb,count))return false;
    int status;
    do{status=platform_pixels_poll(driver);if(!status)runtime_checkpoint();}while(!status);
    return status>0;
}

void platform_pixels_close(void *driver) {
    Pixels *p=driver;
    if(!p)return;
    platform_pixels_cancel(p);
    dma_channel_cleanup((uint)p->dma);dma_channel_unclaim((uint)p->dma);
    pio_sm_set_enabled(p->pio,p->sm,false);pio_sm_unclaim(p->pio,p->sm);
    pio_remove_program(p->pio,&ws2812_program,p->offset);
    gpio_init((uint)p->config.pin);gpio_set_dir((uint)p->config.pin,GPIO_IN);
    free(p->words);free(p);
}
