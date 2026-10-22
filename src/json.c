#include "json.h"
#include <string.h>
#include <limits.h>
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool json_string(const Json *j, int t, char *out, size_t capacity) {
    if (t < 0 || t >= j->count || j->tokens[t].type != JSON_STRING || !capacity) return false;
    size_t used=0;
    for (size_t p=j->tokens[t].start;p<j->tokens[t].end;p++) {
        unsigned char c=(unsigned char)j->text[p]; uint32_t value=c;
        if (c=='\\') {
            c=(unsigned char)j->text[++p];
            const char *esc="\"\\/bfnrt", *decoded="\"\\/\b\f\n\r\t";
            const char *found=strchr(esc,c);
            if (found) value=(unsigned char)decoded[found-esc];
            else if (c=='u') {
                value=0; for (int k=0;k<4;k++) value=value*16u+(unsigned)hex(j->text[++p]);
                if (value>=0xd800 && value<=0xdbff) {
                    if (p+6>=j->tokens[t].end || j->text[p+1]!='\\' || j->text[p+2]!='u') return false;
                    p+=2; uint32_t low=0; for (int k=0;k<4;k++) low=low*16u+(unsigned)hex(j->text[++p]);
                    if (low<0xdc00 || low>0xdfff) return false;
                    value=0x10000+((value-0xd800)<<10)+(low-0xdc00);
                } else if (value>=0xdc00 && value<=0xdfff) return false;
            } else return false;
        } else if (c>=128) {
            unsigned n; uint32_t minimum;
            if (c>=0xc2 && c<=0xdf) { n=1; value=c&31; minimum=0x80; }
            else if (c>=0xe0 && c<=0xef) { n=2; value=c&15; minimum=0x800; }
            else if (c>=0xf0 && c<=0xf4) { n=3; value=c&7; minimum=0x10000; }
            else return false;
            for (unsigned k=0;k<n;k++) { if (++p>=j->tokens[t].end) return false; c=(unsigned char)j->text[p]; if ((c&0xc0)!=0x80) return false; value=(value<<6)|(c&63); }
            if (value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return false;
        }
        if (!value) return false;
        unsigned n=value<0x80?1:value<0x800?2:value<0x10000?3:4;
        if (used+n>=capacity) return false;
        if (n==1) out[used++]=(char)value;
        else { out[used++]=(char)((n==2?0xc0:n==3?0xe0:0xf0)|(value>>(6*(n-1)))); for (unsigned k=n-1;k>0;k--) out[used++]=(char)(0x80|((value>>(6*(k-1)))&63)); }
    }
    out[used]=0; return true;
}
bool json_equal(const Json *j, int t, const char *value) {
    char buffer[128];
    return json_string(j, t, buffer, sizeof(buffer)) && !strcmp(buffer, value);
}
/* Iterative parsing also bounds command stack use: nesting lives in the
 * explicit 17-entry cursor, never in recursive C frames. */
void json_begin(JsonParser *p,Json *j,const void *bytes,size_t size){
    memset(p,0,sizeof(*p));memset(j,0,sizeof(*j));
    p->json=j;j->text=bytes;j->size=size;p->token=-1;
    if(!size||size>UINT16_MAX)p->status=-1;
}
static void completed(JsonParser *p,int index){
    Json *j=p->json;JsonToken *t=&j->tokens[index];t->next=j->count;
    if(!p->depth){p->root_done=true;return;}
    unsigned d=p->depth-1;JsonToken *parent=&j->tokens[p->parents[d]];
    if(parent->type==JSON_OBJECT&&p->phases[d]<2){
        if(t->type!=JSON_STRING||!json_string(j,index,p->key_name,sizeof(p->key_name))){p->status=-1;return;}
        p->key=index;p->sibling=p->parents[d]+1;p->lex=4;
    }else{parent->children++;p->phases[d]=4;}
}
static bool digit(char c){return c>='0'&&c<='9';}
static bool space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static bool delimiter(char c){return space(c)||c==','||c=='}'||c==']';}
int json_step(JsonParser *p,unsigned units){
    Json *j=p->json;
    while(units--&&!p->status){
        if(p->lex==4){
            if(p->sibling<p->key){
                if(json_equal(j,p->sibling,p->key_name)){p->status=-1;break;}
                p->sibling=j->tokens[p->sibling+1].next;
            }else{p->lex=0;p->phases[p->depth-1]=2;}
            continue;
        }
        bool end=j->position==j->size;char c=end?0:j->text[j->position];
        if(!end&&!c){p->status=-1;break;}
        if(p->lex==1){
            if(end){p->status=-1;break;}
            j->position++;
            if(p->unicode){if(hex(c)<0)p->status=-1;else p->unicode--;}
            else if(p->escape){p->escape=0;if(c=='u')p->unicode=4;else if(!strchr("\"\\/bfnrt",c))p->status=-1;}
            else if(c=='\\')p->escape=1;
            else if((unsigned char)c<32)p->status=-1;
            else if(c=='"'){j->tokens[p->token].end=j->position-1;p->lex=0;completed(p,p->token);}
            continue;
        }
        if(p->lex==2){
            unsigned n=p->number;
            if(end||delimiter(c)){
                if(n!=2&&n!=3&&n!=5&&n!=8){p->status=-1;break;}
                j->tokens[p->token].end=j->position;p->lex=0;completed(p,p->token);continue;
            }
            if(n==0&&c=='-')p->number=1;
            else if((n==0||n==1)&&digit(c))p->number=c=='0'?2:3;
            else if(n==3&&digit(c)){}
            else if((n==2||n==3)&&c=='.')p->number=4;
            else if((n==4||n==5)&&digit(c))p->number=5;
            else if((n==2||n==3||n==5)&&(c=='e'||c=='E'))p->number=6;
            else if(n==6&&(c=='+'||c=='-'))p->number=7;
            else if((n==6||n==7||n==8)&&digit(c))p->number=8;
            else{p->status=-1;break;}
            j->position++;continue;
        }
        if(p->lex==3){
            char first=j->text[j->tokens[p->token].start];
            const char *literal=first=='t'?"true":first=='f'?"false":"null";
            if(!literal[p->literal]){j->tokens[p->token].end=j->position;p->lex=0;completed(p,p->token);continue;}
            if(end||c!=literal[p->literal]){p->status=-1;break;}
            p->literal++;j->position++;continue;
        }
        if(space(c)){j->position++;continue;}
        if(p->root_done){p->status=end?1:-1;break;}
        if(end){p->status=-1;break;}
        if(p->depth){
            unsigned d=p->depth-1;int parent=p->parents[d];
            bool object=j->tokens[parent].type==JSON_OBJECT;unsigned phase=p->phases[d];
            char close=object?'}':']';
            if((phase==0||phase==4)&&c==close){j->position++;j->tokens[parent].end=j->position;p->depth--;completed(p,parent);continue;}
            if(phase==4){if(c!=','){p->status=-1;break;}p->phases[d]=1;j->position++;continue;}
            if(phase==2){if(c!=':'){p->status=-1;break;}p->phases[d]=3;j->position++;continue;}
            if(object&&phase<2&&c!='"'){p->status=-1;break;}
        }
        if(p->depth>16||j->count==256){p->status=-1;break;}
        int index=j->count++;JsonToken *t=&j->tokens[index];t->start=j->position;p->token=index;
        if(c=='{'||c=='['){
            t->type=c=='{'?JSON_OBJECT:JSON_ARRAY;
            p->parents[p->depth]=index;p->phases[p->depth++]=0;j->position++;
        }else if(c=='"'){t->type=JSON_STRING;t->start=++j->position;p->lex=1;}
        else{t->type=JSON_PRIMITIVE;p->lex=(c=='t'||c=='f'||c=='n')?3:2;p->number=p->literal=0;}
    }
    return p->status;
}
bool json_parse(Json *j,const void *bytes,size_t size){
    JsonParser parser;json_begin(&parser,j,bytes,size);
    while(!json_step(&parser,256)){}
    return parser.status>0;
}
int json_get(const Json *j, int object, const char *key) {
    if (object < 0 || j->tokens[object].type != JSON_OBJECT) return -1;
    for (int k = object + 1; k < j->tokens[object].next; k = j->tokens[k + 1].next)
        if (json_equal(j, k, key)) return k + 1;
    return -1;
}
bool json_u32(const Json *j, int t, uint32_t *value) {
    if (t < 0 || j->tokens[t].type != JSON_PRIMITIVE) return false;
    uint32_t n = 0;
    for (size_t p = j->tokens[t].start; p < j->tokens[t].end; ++p) {
        char c = j->text[p];
        if (c < '0' || c > '9' || n > (UINT32_MAX - (unsigned)(c - '0')) / 10u) return false;
        n = n * 10u + (unsigned)(c - '0');
    }
    *value = n; return true;
}
bool json_fields(const Json *j, int object, const char *const *allowed) {
    if (object < 0 || j->tokens[object].type != JSON_OBJECT) return false;
    for (int k = object + 1; k < j->tokens[object].next; k = j->tokens[k + 1].next) {
        bool found = false;
        for (size_t n = 0; allowed[n]; ++n) if (json_equal(j, k, allowed[n])) found = true;
        if (!found) return false;
    }
    return true;
}

// Bounded conversion: JSON grammar is already checked, but conversion must also
// reject fractional integers, overflow and non-finite floating-point values.
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
/* Most diagnostic counters fit in 32 bits. Avoid a full printf parser and
 * software 64-bit division for each digit in every state response. */
size_t json_format_u64(char out[21],uint64_t value){
    char reverse[20];size_t count=0;
    while(value>UINT32_MAX){uint64_t next=value/10u;reverse[count++]=(char)('0'+value-next*10u);value=next;}
    uint32_t low=(uint32_t)value;
    do{uint32_t next=low/10u;reverse[count++]=(char)('0'+low-next*10u);low=next;}while(low);
    for(size_t i=0;i<count;i++)out[i]=reverse[count-i-1];
    out[count]=0;return count;
}
static bool primitive(const Json *j,int t,char *buffer,size_t capacity) {
    if (t<0 || j->tokens[t].type!=JSON_PRIMITIVE) return false;
    size_t size=j->tokens[t].end-j->tokens[t].start;
    if (!size || size>=capacity) return false;
    memcpy(buffer,j->text+j->tokens[t].start,size); buffer[size]=0; return true;
}
bool json_i64(const Json *j,int t,int64_t *value) {
    char buffer[32],*end;
    if (!primitive(j,t,buffer,sizeof(buffer))) return false;
    errno=0; long long n=strtoll(buffer,&end,10);
    if (errno || *end || end==buffer || n<INT64_MIN || n>INT64_MAX) return false;
    *value=(int64_t)n; return true;
}
bool json_f64(const Json *j,int t,double *value) {
    char buffer[64],*end;
    if (!primitive(j,t,buffer,sizeof(buffer))) return false;
    errno=0; double n=strtod(buffer,&end);
    // strtod may set ERANGE for a finite subnormal (or rounded zero).
    if (*end || end==buffer || !isfinite(n)) return false;
    *value=n; return true;
}
bool json_bool(const Json *j,int t,bool *value) {
    char buffer[8];
    if (!primitive(j,t,buffer,sizeof(buffer))) return false;
    if (!strcmp(buffer,"true")) { *value=true; return true; }
    if (!strcmp(buffer,"false")) { *value=false; return true; }
    return false;
}
