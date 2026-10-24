#ifndef KAJ_DEVICE_HTTP_SETUP_H
#define KAJ_DEVICE_HTTP_SETUP_H
#include "device/http.h"
bool http_setup_origin(const char *host,const char *origin);
unsigned http_setup(Http *,const HttpRequest *);
#endif
