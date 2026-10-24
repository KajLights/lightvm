#pragma once
#include <stddef.h>
#include <stdint.h>
#ifndef DEVICE_WIFI
#define DEVICE_WIFI 0
#endif
#ifndef DEVICE_HTTP
#define DEVICE_HTTP 0
#endif
#ifndef DEVICE_WEBSOCKET
#define DEVICE_WEBSOCKET 0
#endif
#ifndef DEVICE_HTTP_CLIENTS
#define DEVICE_HTTP_CLIENTS 4u
#endif
#ifndef DEVICE_HTTP_RESPONSE_BYTES
#define DEVICE_HTTP_RESPONSE_BYTES 8192u
#endif
#define DEVICE_CONFIG_LIMIT 8192u
#define DEVICE_NAME_LIMIT 32u
