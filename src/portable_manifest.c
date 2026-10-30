#include "portable.h"
#include "release.h"
#include "outputs.h"
#include "vm_allocator.h"
#include <string.h>

static const char *declarations(Portable *p,const Json *j){
    char compiler[96];
    static const char *const pixel_fields[]={"format",NULL};
    static const char *const setting_fields[]={"name","type","register",NULL};
    static const char *const types[]={"","int","float","rgb8","bool"};
    if(!json_equal(j,json_get(j,0,"schema"),"lightvm.program")||
       !json_string(j,json_get(j,0,"compiler_version"),compiler,sizeof(compiler))||
       !lightvm_release_valid(compiler,false)||
       !json_equal(j,json_get(j,0,"profile"),p->profile==4?"transition":"animation"))return "invalid LightVM manifest";
    int pixels=json_get(j,0,"pixels"),settings=json_get(j,0,"settings");
    if(!json_fields(j,pixels,pixel_fields)||!json_equal(j,json_get(j,pixels,"format"),"rgba8-premul")||
       settings<0||j->tokens[settings].type!=JSON_ARRAY||j->tokens[settings].children!=(int)p->nsettings)return "invalid LightVM manifest";
    unsigned n=0;
    for(int t=settings+1;t<j->tokens[settings].next;t=j->tokens[t].next,n++){
        uint32_t reg;
        if(!json_fields(j,t,setting_fields)||!json_string(j,json_get(j,t,"name"),p->setting_names[n],33)||!object_name_valid(p->setting_names[n])||
           !json_u32(j,json_get(j,t,"register"),&reg)||reg>=p->nregs||reg!=pbc_setting(p,n).reg||
           pbc_type(p,reg)<T_INT||pbc_type(p,reg)>T_BOOL||!json_equal(j,json_get(j,t,"type"),types[pbc_type(p,reg)]))return "invalid LightVM settings manifest";
        for(unsigned k=0;k<n;k++)if(!strcmp(p->setting_names[k],p->setting_names[n]))return "duplicate setting name";
    }
    return inputs_load(p,j);
}

const char *pbc_manifest_decode(Portable *p,const Json *j){
    /* One milestone baseline covers every supported encoding and callback.
       Validate it before admitting any declarations, calls or instructions. */
    char required[96];
    if(!json_string(j,json_get(j,0,"requires"),required,sizeof(required)))return "invalid required LightVM release";
    const char *error=lightvm_release_error(LIGHTVM_VERSION,required);if(error)return error;
    if(!lightvm_release_at_least(required,LIGHTVM_BASELINE))return "program requires a release below the supported LightVM baseline " LIGHTVM_BASELINE;
    return p->profile==3||p->profile==4?declarations(p,j):inputs_load(p,j);
}
