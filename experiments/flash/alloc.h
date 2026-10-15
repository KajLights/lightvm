#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
void *lightvm_toy_malloc(size_t);
void *demo_calloc(size_t, size_t);
void *demo_realloc(void *, size_t);
void demo_free(void *);
int lightvm_toy_vasprintf(char **,const char *,va_list);
#define vasprintf lightvm_toy_vasprintf
#define malloc(size) lightvm_toy_malloc(size)
#define calloc demo_calloc
#define realloc demo_realloc
#define free demo_free
