#ifndef LIGHTVM_HOST_PORT_H
#define LIGHTVM_HOST_PORT_H
#include <stdbool.h>
struct AudioSources;
void host_audio_init(struct AudioSources *);
void host_audio_poll(struct AudioSources *);
bool host_storage_open(const char *path);
void host_storage_close(void);
#endif
