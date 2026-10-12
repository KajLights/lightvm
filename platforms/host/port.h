#ifndef LIGHTVM_HOST_PORT_H
#define LIGHTVM_HOST_PORT_H
#include <stdbool.h>
bool host_storage_open(const char *path);
void host_storage_close(void);
#endif
