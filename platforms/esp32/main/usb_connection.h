#ifndef LIGHTVM_ESP32_USB_CONNECTION_H
#define LIGHTVM_ESP32_USB_CONNECTION_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t missing_since_us;
    bool connected, missing;
} Esp32UsbConnection;

/* The SDK infers connection from USB SOF timing. A transient missing SOF must
   not discard an upload or staged configuration between two valid requests.
   Traffic is positive connection evidence even when that indication is stale. */
static inline bool esp32_usb_disconnected(Esp32UsbConnection *link,
                                         bool connected, bool received,
                                         uint64_t now_us) {
    if (connected || received) {
        link->connected=true;
        link->missing=false;
    } else if (link->connected) {
        if (!link->missing) {
            link->missing=true;
            link->missing_since_us=now_us;
        } else if (now_us-link->missing_since_us>=100000u) {
            link->connected=false;
            link->missing=false;
            return true;
        }
    }
    return false;
}
#endif
