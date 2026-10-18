/* Capture and exercise the production Pico DMA driver on hardware. */
#include "../../platforms/pico/pixels.c"
#include "monitor.pio.h"
#include "pico/stdio_usb.h"
#include "hardware/clocks.h"
#include <stdio.h>
#include <string.h>

#define COUNT 200u
#define SAMPLES 40u
static const int probe_pins[]={16,17,15,27};
static uint8_t source[COUNT*3];
static uint32_t received[COUNT],expected[COUNT];
static volatile uint32_t useful_work;

static void pattern(unsigned brightness,const char *order) {
    memset(source,0,sizeof(source));
    for(unsigned i=0;i<COUNT;i++)source[i*3+i%3]=(uint8_t)(i*17u+3u);
    for(unsigned i=0;i<COUNT;i++){
        uint32_t word=0;
        for(unsigned c=0;c<3;c++){
            unsigned channel=order[c]=='R'?0:order[c]=='G'?1:2;
            word=(word<<8)|((source[i*3+channel]*brightness+127u)/255u);
        }
        expected[i]=word;
    }
}

static bool capture(unsigned pin,const char *order,unsigned brightness) {
    PixelConfig cfg={.pin=(int32_t)pin,.count=COUNT,.brightness=brightness};
    memcpy(cfg.order,order,4);
    void *driver=NULL;
    if(!platform_pixels_open(&cfg,&driver))return false;
    PIO rx=pio1;
    int sm=pio_claim_unused_sm(rx,false),dma=dma_claim_unused_channel(false);
    if(sm<0||dma<0){
        if(sm>=0)pio_sm_unclaim(rx,(uint)sm);
        if(dma>=0)dma_channel_unclaim((uint)dma);
        platform_pixels_close(driver);return false;
    }
    uint offset=pio_add_program(rx,&output_monitor_program);
    pio_sm_config c=output_monitor_program_get_default_config(offset);
    sm_config_set_in_pins(&c,pin);
    sm_config_set_in_shift(&c,false,true,24);
    sm_config_set_fifo_join(&c,PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c,(float)clock_get_hz(clk_sys)/8000000.0f);
    /* Do not change the pin mux: another PIO drives it; both can observe it. */
    pio_sm_init(rx,(uint)sm,offset,&c);
    dma_channel_config dc=dma_channel_get_default_config((uint)dma);
    channel_config_set_transfer_data_size(&dc,DMA_SIZE_32);
    channel_config_set_read_increment(&dc,false);
    channel_config_set_write_increment(&dc,true);
    channel_config_set_dreq(&dc,pio_get_dreq(rx,(uint)sm,false));
    memset(received,0xa5,sizeof(received));
    dma_channel_configure((uint)dma,&dc,received,&rx->rxf[sm],COUNT,true);
    pio_sm_set_enabled(rx,(uint)sm,true);
    pattern(brightness,order);
    bool ok=platform_pixels_submit(driver,source,COUNT);
    /* The caller's RGB buffer is free to change once copied to DMA staging. */
    memset(source,0,sizeof(source));
    uint64_t deadline=time_us_64()+100000;
    while(dma_channel_is_busy((uint)dma)&&time_us_64()<deadline)tight_loop_contents();
    bool timed_out=dma_channel_is_busy((uint)dma);
    if(timed_out){dma_channel_abort((uint)dma);ok=false;}
    platform_pixels_cancel(driver);
    for(unsigned i=0;i<COUNT;i++)if((received[i]&0xffffffu)!=expected[i]){
        printf("{\"capture_mismatch\":%u,\"got\":%lu,\"expected\":%lu}\n",i,
               (unsigned long)received[i],(unsigned long)expected[i]);ok=false;break;
    }
    pio_sm_set_enabled(rx,(uint)sm,false);pio_sm_unclaim(rx,(uint)sm);
    pio_remove_program(rx,&output_monitor_program,offset);
    dma_channel_unclaim((uint)dma);platform_pixels_close(driver);
    printf("{\"capture_pin\":%u,\"order\":\"%s\",\"brightness\":%u,\"pixels\":%u,\"timeout\":%s,\"passed\":%s}\n",
           pin,order,brightness,COUNT,timed_out?"true":"false",ok?"true":"false");
    return ok;
}

static int compare_u32(const void *a,const void *b) {
    uint32_t aa=*(const uint32_t *)a,bb=*(const uint32_t *)b;
    return aa<bb?-1:aa>bb;
}
static uint32_t median(uint32_t *values) {
    qsort(values,SAMPLES,sizeof(*values),compare_u32);
    return (values[SAMPLES/2-1]+values[SAMPLES/2])/2;
}

static bool benchmark(unsigned devices,unsigned brightness) {
    void *outputs[4]={0};
    uint32_t submission[SAMPLES],wall[SAMPLES],work[SAMPLES];
    bool ok=true;
    for(unsigned d=0;d<devices;d++){
        PixelConfig cfg={.pin=probe_pins[d],.count=COUNT,.brightness=brightness,.order="GRB"};
        if(!(platform_pixels_open(&cfg,&outputs[d]))){ok=false;break;}
    }
    pattern(brightness,"GRB");
    if(ok)for(unsigned sample=0;sample<SAMPLES;sample++){
        uint64_t started=time_us_64();
        for(unsigned d=0;d<devices;d++)ok&=platform_pixels_submit(outputs[d],source,COUNT);
        submission[sample]=(uint32_t)(time_us_64()-started);
        unsigned iterations=0;
        for(;;){
            bool done=true;
            for(unsigned d=0;d<devices;d++)if(!platform_pixels_poll(outputs[d]))done=false;
            if(done)break;
            useful_work=useful_work*1664525u+1013904223u;iterations++;
        }
        work[sample]=iterations;wall[sample]=(uint32_t)(time_us_64()-started);
    }
    for(unsigned d=0;d<devices;d++){
        platform_pixels_close(outputs[d]);
    }
    if(ok)printf("{\"benchmark\":\"%s\",\"devices\":%u,\"brightness\":%u,\"pixels_each\":%u,\"samples\":%u,\"submit_us_median\":%lu,\"wall_us_median\":%lu,\"background_iterations_median\":%lu}\n",
        "dma",devices,brightness,COUNT,SAMPLES,
        (unsigned long)median(submission),(unsigned long)median(wall),(unsigned long)median(work));
    return ok;
}

static bool lifecycle(void) {
    pattern(100,"GRB");
    for(unsigned n=0;n<64;n++){
        void *output=NULL;
        PixelConfig cfg={.pin=probe_pins[n%4],.count=COUNT,.brightness=100,.order="GRB"};
        if(!platform_pixels_open(&cfg,&output))return false;
        bool ok=!platform_pixels_submit(output,source,COUNT-1)&&platform_pixels_submit(output,source,COUNT)&&
            !platform_pixels_submit(output,source,COUNT);
        /* Close immediately while DMA is active, then reclaim on next round. */
        platform_pixels_close(output);
        if(!ok)return false;
    }
    /* Force DMA exhaustion and ensure failed opens release their PIO resource. */
    uint32_t claimed=0;
    for(unsigned n=0;n<NUM_DMA_CHANNELS;n++)if(!dma_channel_is_claimed(n)){dma_channel_claim(n);claimed|=1u<<n;}
    void *output=NULL;
    PixelConfig cfg={.pin=16,.count=COUNT,.brightness=100,.order="GRB"};
    bool ok=!platform_pixels_open(&cfg,&output);
    for(unsigned n=0;n<NUM_DMA_CHANNELS;n++)if(claimed&(1u<<n))dma_channel_unclaim(n);
    ok&=platform_pixels_open(&cfg,&output);
    if(output)platform_pixels_close(output);
    printf("{\"lifecycle_iterations\":64,\"invalid_count_busy_rejection_close_reopen_dma_exhaustion\":%s}\n",ok?"true":"false");
    return ok;
}

static bool fault_recovery(void) {
    void *driver=NULL;
    PixelConfig cfg={.pin=16,.count=COUNT,.brightness=100,.order="GRB"};
    if(!platform_pixels_open(&cfg,&driver))return false;
    Pixels *p=driver;
    pattern(100,"GRB");
    /* A stopped PIO cannot consume DMA data. Force its deadline to exercise
     * bounded abort, FIFO/shift-register reset and subsequent successful retry. */
    pio_sm_set_enabled(p->pio,p->sm,false);
    bool ok=platform_pixels_submit(p,source,COUNT);
    p->deadline=time_us_64()-1;
    ok&=platform_pixels_poll(p)==-1&&!dma_channel_is_busy((uint)p->dma)&&!p->pending;
    ok&=platform_pixels_show(p,source,COUNT);
    pio_sm_set_enabled(p->pio,p->sm,false);
    ok&=platform_pixels_submit(p,source,COUNT);
    p->deadline=time_us_64()-1;
    platform_pixels_cancel(p);
    ok&=!dma_channel_is_busy((uint)p->dma)&&platform_pixels_show(p,source,COUNT);
    /* Late polling after a successful transfer must not create a false timeout. */
    ok&=platform_pixels_submit(p,source,COUNT);
    sleep_us(COUNT*30+400);p->deadline=time_us_64()-1;
    ok&=platform_pixels_poll(p)==0;sleep_us(340);
    ok&=platform_pixels_poll(p)==1;
    platform_pixels_close(p);
    printf("{\"stalled_dma_timeout_cancel_retry_and_late_poll\":%s}\n",ok?"true":"false");
    return ok;
}

int main(void) {
    stdio_init_all();
    while(!stdio_usb_connected())sleep_ms(10);
    puts("{\"ready\":true}");
    while(getchar_timeout_us(100000)!='r')tight_loop_contents();
    printf("{\"clock_hz\":%lu,\"dma_driver_bytes\":%u,\"staging_bytes_per_200_pixels\":%u}\n",
           (unsigned long)clock_get_hz(clk_sys),(unsigned)sizeof(Pixels),COUNT*4);
    bool ok=true;
    const char *orders[]={"RGB","RBG","GRB","GBR","BRG","BGR"};
    const unsigned brightness[]={0,100,255};
    for(unsigned b=0;b<3;b++)for(unsigned o=0;o<6;o++)ok&=capture(b==2?17:16,orders[o],brightness[b]);
    for(unsigned p=1;p<4;p++)ok&=capture((unsigned)probe_pins[p],"GRB",100);
    for(unsigned n=1;n<=4;n*=2){ok&=benchmark(n,100);ok&=benchmark(n,255);}
    ok&=lifecycle();
    ok&=fault_recovery();
    printf("{\"done\":true,\"passed\":%s}\n",ok?"true":"false");
    stdio_flush();
    for(;;)sleep_ms(100);
}
