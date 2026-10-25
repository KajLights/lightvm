#include "noise.h"
#include "portable.h"
#include "release.h"
#include "outputs.h"
#include "vm_allocator.h"
#include <string.h>

static const char *declarations(Portable *p,const Json *j){
    char required[96],compiler[96];
    static const char *const pixel_fields[]={"format",NULL};
    static const char *const setting_fields[]={"name","type","register",NULL};
    static const char *const types[]={"","int","float","rgb8","bool"};
    if(!json_equal(j,json_get(j,0,"schema"),"lightvm.program")||
       !json_string(j,json_get(j,0,"requires"),required,sizeof(required))||
       !json_string(j,json_get(j,0,"compiler_version"),compiler,sizeof(compiler))||
       !lightvm_release_valid(compiler,false)||
       !json_equal(j,json_get(j,0,"profile"),p->profile==4?"transition":"animation"))return "invalid LightVM manifest";
    const char *error=lightvm_release_error(LIGHTVM_VERSION,required);if(error)return error;
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
    const char *error=p->profile==3||p->profile==4?declarations(p,j):inputs_load(p,j);
    if(error)return error;
    /* Generic typed callbacks also declare the application floor when using
       new numeric features; a profile binding cannot bypass that contract. */
    int token=json_get(j,0,"requires");
    if(token>=0){
        char required[96];
        if(!json_string(j,token,required,sizeof(required)))return "invalid required LightVM release";
        error=lightvm_release_error(LIGHTVM_VERSION,required);if(error)return error;
        p->numeric_allowed=lightvm_release_at_least(required,LBC_NUMERIC_RELEASE);
        p->kernels_allowed=lightvm_release_at_least(required,LBC_KERNEL_RELEASE);
        p->noise_allowed=lightvm_release_at_least(required,LVM_NOISE_RELEASE);
    }
    for(unsigned r=0;r<p->nregs;r++)if(pbc_type(p,r)==T_FIXED&&!p->numeric_allowed)
        return "fixed values require LightVM " LBC_NUMERIC_RELEASE;
    return NULL;
}
