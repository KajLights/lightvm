#pragma once
#include "device/host.h"
/* Calls and all portable state belong to one management task. */
void device_esp32_init(DeviceHost *,bool low_latency);
void device_esp32_poll(void);
