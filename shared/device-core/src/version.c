#include "device/version.h"
#include <stdint.h>
#include <string.h>

typedef struct { uint32_t core[3]; const char *pre; size_t pre_size; } Release;
static bool digit(char c) { return c>='0'&&c<='9'; }
static bool identifier(char c) { return digit(c)||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'; }
static bool parse(const char *text,bool shorthand,Release *out) {
    if(!text||!text[0]||strlen(text)>95)return false;
    memset(out,0,sizeof(*out));const char *p=text;unsigned count=0;
    for(;;){
        const char *start=p;uint32_t value=0;
        if(!digit(*p))return false;
        while(digit(*p)){unsigned d=(unsigned)(*p++-'0');if(value>(UINT32_MAX-d)/10)return false;value=value*10+d;}
        if(p-start>1&&*start=='0')return false;
        out->core[count++]=value;
        if(*p!='.'||count==3)break;
        p++;
    }
    if(count<3&&(!shorthand||*p=='-'||*p=='+'))return false;
    if(*p=='-'){
        out->pre=++p;
        for(;;){
            const char *start=p;bool numeric=true;
            while(identifier(*p)){if(!digit(*p))numeric=false;p++;}
            if(p==start||(numeric&&p-start>1&&*start=='0'))return false;
            if(*p!='.')break;
            p++;
        }
        out->pre_size=(size_t)(p-out->pre);
    }
    if(*p=='+'){
        p++;
        for(;;){const char *start=p;while(identifier(*p))p++;if(p==start)return false;if(*p!='.')break;p++;}
    }
    return !*p;
}
bool device_version_valid(const char *value,bool shorthand) { Release r;return parse(value,shorthand,&r); }
static int prerelease(const Release *a,const Release *b){
    if(!a->pre_size||!b->pre_size)return !a->pre_size-!b->pre_size;
    size_t ai=0,bi=0;
    while(ai<a->pre_size&&bi<b->pre_size){
        size_t ae=ai,be=bi;bool an=true,bn=true;
        while(ae<a->pre_size&&a->pre[ae]!='.'){if(!digit(a->pre[ae]))an=false;ae++;}
        while(be<b->pre_size&&b->pre[be]!='.'){if(!digit(b->pre[be]))bn=false;be++;}
        size_t al=ae-ai,bl=be-bi;int order=0;
        if(an!=bn)order=an?-1:1;
        else if(an&&al!=bl)order=al<bl?-1:1;
        else {order=memcmp(a->pre+ai,b->pre+bi,al<bl?al:bl);if(!order&&al!=bl)order=al<bl?-1:1;}
        if(order)return order;
        ai=ae;bi=be;if(ai<a->pre_size)ai++;if(bi<b->pre_size)bi++;
    }
    return (ai<a->pre_size)-(bi<b->pre_size);
}
static const char *compatible(Release a,Release b){
    if(a.core[0]!=b.core[0])return "program requires a different device major release";
    for(unsigned n=1;n<3;n++)if(a.core[n]!=b.core[n])return a.core[n]<b.core[n]?"program requires a newer device release":NULL;
    return prerelease(&a,&b)<0?"program requires a newer device release":NULL;
}
const char *device_version_error(const char *current,const char *required){
    Release a,b;
    if(!parse(current,false,&a))return "invalid device application release";
    if(!parse(required,true,&b))return "invalid minimum required device release";
    return compatible(a,b);
}
bool device_version_at_least(const char *value,const char *floor){
    Release a,b;
    return parse(value,true,&a)&&parse(floor,true,&b)&&!compatible(a,b);
}
