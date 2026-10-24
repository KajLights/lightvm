#ifndef LIGHTVM_STATUS_H
#define LIGHTVM_STATUS_H
#include "device/json.h"
#include <string.h>
/* Bounded key/value diagnostics. Keep large printf frames and generic integer
 * conversion off the frequent STATUS path used by USB and HTTP polling. */
typedef struct { char *text;size_t capacity,used; } StatusWriter;
static inline void status_literal(StatusWriter *w,const char *text,size_t limit){
    while(*text&&limit--&&w->used+1<w->capacity)w->text[w->used++]=*text++;
    if(w->capacity)w->text[w->used]=0;
}
static inline void status_field(StatusWriter *w,const char *key,const char *text,size_t limit){
    if(w->used)status_literal(w," ",1);
    status_literal(w,key,SIZE_MAX);status_literal(w,"=",1);status_literal(w,text,limit);
}
static inline void status_integer(StatusWriter *w,const char *key,uint64_t value){
    char text[21];json_format_u64(text,value);status_field(w,key,text,sizeof(text));
}
#endif
