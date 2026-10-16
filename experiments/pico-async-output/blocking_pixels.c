/* Unchanged blocking driver retained as the A/B measurement control. */
typedef struct { PIO pio; uint sm,offset; PixelConfig config; } BlockingPixels;
static bool blocking_pixels_open(const PixelConfig *config,void **driver) {
    BlockingPixels *p=calloc(1,sizeof(*p)); if (!p) return false;
    p->config=*config;int sm=-1;
    const PIO blocks[]={pio0,pio1};
    for(unsigned n=0;n<2;n++){
        p->pio=blocks[n];
        if(!pio_can_add_program(p->pio,&ws2812_program))continue;
        sm=pio_claim_unused_sm(p->pio,false);if(sm>=0)break;
    }
    if(sm<0){free(p);return false;}
    p->sm=(uint)sm; p->offset=pio_add_program(p->pio,&ws2812_program);
    ws2812_program_init(p->pio,p->sm,p->offset,(uint)config->pin,800000,false);
    *driver=p; return true;
}
static void blocking_pixels_close(void *driver) {
    BlockingPixels *p=driver; if (!p) return;
    pio_sm_set_enabled(p->pio,p->sm,false); pio_sm_unclaim(p->pio,p->sm);
    pio_remove_program(p->pio,&ws2812_program,p->offset);
    gpio_init((uint)p->config.pin); gpio_set_dir((uint)p->config.pin,GPIO_IN); free(p);
}
static bool blocking_pixels_show(void *driver,const uint8_t *rgb,size_t count) {
    BlockingPixels *p=driver; if (!p || count!=p->config.count) return false;
    // Feed each device's PIO directly and finish the wire frame before returning.
    // The shared submit/poll adapter treats this synchronous transfer as complete.
    for (size_t i=0;i<count;++i) {
        uint32_t word=0;
        for (size_t c=0;c<3;++c) {
            unsigned channel=p->config.order[c]=='R' ? 0 : p->config.order[c]=='G' ? 1 : 2;
            word=(word<<8)|((rgb[i*3+channel]*p->config.brightness+127u)/255u);
        }
        pio_sm_put_blocking(p->pio,p->sm,word<<8);
    }
    while (!pio_sm_is_tx_fifo_empty(p->pio,p->sm)) tight_loop_contents();
    sleep_us(330); return true;
}
