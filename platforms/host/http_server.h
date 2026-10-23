#include "app.h"
#ifndef LIGHTVM_HOST_HTTP_SERVER_H
#define LIGHTVM_HOST_HTTP_SERVER_H
#include "device/http.h"
bool host_http_start(App *,const char *address,unsigned port);
void host_http_poll(void);
void host_http_stop(void);
#endif
