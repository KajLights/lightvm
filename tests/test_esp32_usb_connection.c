#include "../platforms/esp32/main/usb_connection.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(void) {
    Esp32UsbConnection link={0};
    CHECK(!esp32_usb_disconnected(&link,false,false,0));
    CHECK(!esp32_usb_disconnected(&link,false,false,200000));
    CHECK(!esp32_usb_disconnected(&link,true,false,300000));
    /* Separate short gaps never accumulate into a disconnect. */
    for (uint64_t now=400000;now<1000000;now+=10000) {
        CHECK(!esp32_usb_disconnected(&link,false,false,now));
        CHECK(!esp32_usb_disconnected(&link,true,false,now+5000));
    }
    CHECK(!esp32_usb_disconnected(&link,false,false,1000000));
    CHECK(!esp32_usb_disconnected(&link,false,false,1099999));
    /* A received request cancels the pending loss even with stale SDK state. */
    CHECK(!esp32_usb_disconnected(&link,false,true,1100000));
    CHECK(!esp32_usb_disconnected(&link,false,false,1200000));
    CHECK(!esp32_usb_disconnected(&link,false,false,1299999));
    CHECK(esp32_usb_disconnected(&link,false,false,1300000));
    CHECK(!esp32_usb_disconnected(&link,false,false,1500000));
    CHECK(!esp32_usb_disconnected(&link,true,false,1600000));
    CHECK(!esp32_usb_disconnected(&link,false,false,1700000));
    CHECK(esp32_usb_disconnected(&link,false,false,1800000));
    puts("ESP USB transient loss, traffic recovery and confirmed disconnect passed.");
    return 0;
}
