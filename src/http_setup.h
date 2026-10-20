#ifndef LIGHTVM_HTTP_SETUP_H
#define LIGHTVM_HTTP_SETUP_H
#include "http.h"
bool http_setup_origin(const char *host,const char *origin);
unsigned http_setup(Http *,const HttpRequest *);
#endif
