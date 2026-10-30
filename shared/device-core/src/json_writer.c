#include "device/description.h"
#include <stdio.h>
void device_json_write_string(DeviceDescriptionWrite write,void *ctx,const char *p){
    write(ctx,"\"");for(;*p;p++){char out[7]={*p,0};unsigned c=(unsigned char)*p;
        if(c<32)snprintf(out,sizeof(out),"\\u%04x",c);
        else if(*p=='"'||*p=='\\'){out[0]='\\';out[1]=*p;out[2]=0;}
        write(ctx,out);
    }write(ctx,"\"");
}
