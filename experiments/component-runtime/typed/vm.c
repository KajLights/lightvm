#ifndef TYPED_VM_CORE_ONLY
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* There is no pointer-valued guest register. Integers, view handles, state and
 * return addresses occupy distinct, bounded stores. One dispatch is a safe point. */
enum { REGS=16, VIEWS=2, STATES=8, STACK=8, SLOTS=16, QUANTUM=61,
       MAX_CODE=4096, JOB_BUDGET=100000, PIXELS=300 };
enum { HALT, CONST, MOV, ADD, XOR, MUL, SHL, SHR, AND, ADDI, LT, JNZ,
       JMP, LOAD8, STORE8, STATE_LOAD, STATE_STORE, CALL, RET };
enum { READY, YIELDED, DONE, FAULT, CANCELLED };
typedef struct { size_t live, peak, largest, limit, allocations; int fail_after; } Memory;
typedef struct { uint32_t slot, generation, length; bool write; } View;
typedef struct Buffer { size_t refs; uint32_t slot, generation, length, locks; uint8_t data[]; } Buffer;
typedef struct { Buffer *slots[SLOTS]; uint32_t generations[SLOTS]; Memory *mem; } Pool;
typedef struct Program {
    uint32_t count, init, frame, state_words, stack_limit, validated, refs;
    size_t bytes;
#ifdef TYPED_VM_CORE_ONLY
    const uint8_t *raw; /* Device: borrow the core-owned uploaded package. */
#else
    uint8_t raw[];
#endif
} Program;
typedef struct {
    Program *program; Pool *pool; Buffer *owned; View views[VIEWS];
    uint32_t regs[REGS], state[STATES], returns[STACK], pc, sp, used, executed;
    int status; bool cancel;
} Instance;
#ifndef TYPED_VM_CORE_ONLY
typedef struct { Buffer *buffer; uint32_t hash; unsigned ticks; } Output;
typedef struct { int pair[2]; uint64_t sent, received, ticks; } Service;
typedef struct { const char *name; bool passed; } Check;
static Check checks[64]; static unsigned nchecks;
static uint64_t resume_ns, resume_max_ns, resume_calls;
#if defined(__APPLE__) || defined(CLOCK_MONOTONIC_RAW)
#define TIMING_CLOCK CLOCK_MONOTONIC_RAW
#else
#define TIMING_CLOCK CLOCK_MONOTONIC
#endif
static uint64_t now_ns(void) { struct timespec t; (void)clock_gettime(TIMING_CLOCK, &t); return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec; }
#endif
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
#ifndef TYPED_VM_CORE_ONLY
static void put32(uint8_t *p, uint32_t n) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8)); }
static void check(const char *name, bool pass) { checks[nchecks++]=(Check){name,pass}; if(!pass) fprintf(stderr,"FAILED: %s\n",name); }
#endif
static void *take(Memory *m, size_t n) {
    if (m->live>m->limit || n>m->limit-m->live || m->fail_after==0) return NULL;
    if(m->fail_after>0) --m->fail_after;
    void *p=calloc(1,n); if(!p) return NULL;
    m->live+=n; m->allocations++; if(m->live>m->peak)m->peak=m->live;
    if(n>m->largest)m->largest=n;
    return p;
}
static void give(Memory *m, void *p, size_t n) { if(p){m->live-=n; free(p);} }
#ifndef TYPED_VM_CORE_ONLY
static uint32_t hash_bytes(const uint8_t *p, size_t n) { uint32_t h=2166136261u; for(size_t i=0;i<n;i++){h^=p[i];h*=16777619u;} return h; }
#endif
static Buffer *buffer_new(Pool *p, uint32_t n) {
    if((uint64_t)n+sizeof(Buffer)>SIZE_MAX)return NULL;
    for(unsigned i=0;i<SLOTS;i++) if(!p->slots[i] && p->generations[i]!=UINT32_MAX) {
        Buffer *b=take(p->mem,sizeof(*b)+n); if(!b)return NULL;
        b->refs=1;b->length=n;b->slot=i;b->generation=++p->generations[i];p->slots[i]=b;return b;
    }
    return NULL;
}
static void buffer_release(Pool *p, Buffer *b) { if(b && --b->refs==0){p->slots[b->slot]=NULL;give(p->mem,b,sizeof(*b)+b->length);} }
static View view_of(Buffer *b) { return (View){b->slot,b->generation,b->length,true}; }
static uint8_t *resolve(Pool *p, View v, uint32_t index, uint32_t bytes, bool write) {
    if(v.slot>=SLOTS)return NULL;
    Buffer *b=p->slots[v.slot];
    if(!b || b->generation!=v.generation || v.length>b->length || index>v.length || bytes>v.length-index)return NULL;
    if(write && (!v.write || b->locks))return NULL;
    return b->data+index;
}
#ifndef TYPED_VM_CORE_ONLY
static bool bulk_invert(Pool *p, View v) { uint8_t *data=resolve(p,v,0,v.length,true);if(!data)return false;for(uint32_t i=0;i<v.length;i++)data[i]^=255;return true; }
#endif
static size_t program_allocation_size(size_t bytes) {
#ifdef TYPED_VM_CORE_ONLY
    (void)bytes;
    return sizeof(Program);
#else
    return sizeof(Program)+bytes;
#endif
}
static void program_release(Memory *m, Program *p) { if(p && --p->refs==0)give(m,p,program_allocation_size(p->bytes)); }
static Program *load_begin(Memory *m,const uint8_t *raw,size_t len) {
    if(len<32 || memcmp(raw,"TVM1",4) || u32(raw+4)!=1)return NULL;
    uint32_t count=u32(raw+8);
    if(!count || count>MAX_CODE || len!=32+(size_t)count*8 || u32(raw+12)>=count || u32(raw+16)>=count || u32(raw+20)>STATES || !u32(raw+24) || u32(raw+24)>STACK || u32(raw+28)!=1)return NULL;
    Program *p=take(m,program_allocation_size(len));if(!p)return NULL;
    p->count=count;p->init=u32(raw+12);p->frame=u32(raw+16);p->state_words=u32(raw+20);p->stack_limit=u32(raw+24);p->bytes=len;p->refs=1;
#ifdef TYPED_VM_CORE_ONLY
    p->raw=raw;
#else
    memcpy(p->raw,raw,len);
#endif
    return p;
}
static bool valid_instruction(const Program *p,const uint8_t *i) {
    uint8_t op=i[0],a=i[1],b=i[2],c=i[3];uint32_t imm=u32(i+4);
    switch(op) {
    case HALT: case RET: return a==0 && b==0 && c==0 && imm==0;
    case CONST: return a<REGS && b==0 && c==0;
    case MOV: return a<REGS && b<REGS && c==0 && imm==0;
    case ADD: case XOR: case MUL: case AND: case LT: return a<REGS && b<REGS && c<REGS && imm==0;
    case SHL: case SHR: return a<REGS && b<REGS && c==0 && imm<32;
    case ADDI: return a<REGS && b<REGS && c==0;
    case JNZ: return a<REGS && b==0 && c==0 && imm<p->count;
    case JMP: case CALL: return a==0 && b==0 && c==0 && imm<p->count;
    case LOAD8: case STORE8: return a<REGS && b<REGS && c<VIEWS;
    case STATE_LOAD: case STATE_STORE: return a<REGS && b==0 && c==0 && imm<p->state_words;
    default:return false;
    }
}
/* -1 invalid, 0 more validation required, 1 complete. */
static int load_step(Program *p,unsigned quota) {
    while(quota-- && p->validated<p->count) {
        if(!valid_instruction(p,p->raw+32+8*p->validated))return -1;
        p->validated++;
    }return p->validated==p->count;
}
static Instance *instance_new(Pool *pool,Program *p,uint32_t seed,uint32_t pixels) {
    if(pixels>UINT32_MAX/3)return NULL;
#ifndef TYPED_VM_CORE_ONLY
    if(p->validated!=p->count)return NULL;
#endif
    Instance *i=take(pool->mem,sizeof(*i));if(!i)return NULL;
    i->owned=buffer_new(pool,pixels*3);if(!i->owned){give(pool->mem,i,sizeof(*i));return NULL;}
    i->program=p;p->refs++;i->pool=pool;i->views[0]=view_of(i->owned);i->regs[0]=seed;i->regs[2]=pixels;i->pc=p->init;return i;
}
static void instance_free(Instance *i) { if(i){Memory *m=i->pool->mem;buffer_release(i->pool,i->owned);program_release(m,i->program);give(m,i,sizeof(*i));} }
static void frame_start(Instance *i,uint32_t frame) { i->regs[1]=frame;i->pc=i->program->frame;i->sp=0;i->used=0;i->status=READY;i->cancel=false; }
static int resume(Instance *i,unsigned quota) {
#ifndef TYPED_VM_CORE_ONLY
    uint64_t start=now_ns();
#endif
    if(i->program->validated!=i->program->count){i->status=FAULT;return FAULT;}
    if(i->status==DONE || i->status==FAULT || i->status==CANCELLED)return i->status;
    i->status=YIELDED;
    while(quota--){
        if(i->cancel){i->status=CANCELLED;break;}
        if(i->used++>=JOB_BUDGET || i->pc>=i->program->count){i->status=FAULT;break;}
        const uint8_t *ins=i->program->raw+32+8*i->pc++;
        uint8_t op=ins[0],a=ins[1],b=ins[2],c=ins[3];uint32_t imm=u32(ins+4);uint32_t *r=i->regs;i->executed++;
        switch(op){
        case HALT:i->status=DONE;break;
        case CONST:r[a]=imm;break;
        case MOV:r[a]=r[b];break;
        case ADD:r[a]=r[b]+r[c];break;
        case XOR:r[a]=r[b]^r[c];break;
        case MUL:r[a]=r[b]*r[c];break;
        case SHL:r[a]=r[b]<<imm;break;
        case SHR:r[a]=r[b]>>imm;break;
        case AND:r[a]=r[b]&r[c];break;
        case ADDI:r[a]=r[b]+imm;break;
        case LT:r[a]=(uint32_t)(r[b]<r[c]);break;
        case JNZ:if(r[a])i->pc=imm;break;
        case JMP:i->pc=imm;break;
        case STATE_LOAD:r[a]=i->state[imm];break;
        case STATE_STORE:i->state[imm]=r[a];break;
        case CALL:if(i->sp>=i->program->stack_limit)i->status=FAULT;else{i->returns[i->sp++]=i->pc;i->pc=imm;}break;
        case RET:if(!i->sp)i->status=FAULT;else i->pc=i->returns[--i->sp];break;
        case LOAD8:case STORE8:{
            uint32_t offset_reg=op==STORE8?a:b;
            uint8_t *ptr=NULL;
            if(r[offset_reg]<=UINT32_MAX-imm)ptr=resolve(i->pool,i->views[c],r[offset_reg]+imm,1,op==STORE8);
            if(!ptr)i->status=FAULT;else if(op==STORE8)*ptr=(uint8_t)r[b];else r[a]=*ptr;
            break;
        }
        default:i->status=FAULT;break;
        }
        if(i->status!=YIELDED)break;
    }
#ifndef TYPED_VM_CORE_ONLY
    uint64_t elapsed=now_ns()-start;resume_ns+=elapsed;resume_calls++;if(elapsed>resume_max_ns)resume_max_ns=elapsed;
#endif
    return i->status;
}
#ifndef TYPED_VM_CORE_ONLY
static bool service_open(Service *s) {
    if(socketpair(AF_UNIX,SOCK_STREAM,0,s->pair))return false;
    for(unsigned i=0;i<2;i++){int flags=fcntl(s->pair[i],F_GETFL);if(flags<0 || fcntl(s->pair[i],F_SETFL,flags|O_NONBLOCK)<0){close(s->pair[0]);close(s->pair[1]);return false;}}
    return true;
}
static bool service_tick(Service *s) {
    uint8_t b=0xa5,buf[16];ssize_t n=write(s->pair[0],&b,1);
    if(n==1)s->sent++;else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)return false;
    n=read(s->pair[1],buf,sizeof(buf));
    if(n>0){for(ssize_t j=0;j<n;j++)if(buf[j]!=0xa5)return false;s->received+=(uint64_t)n;}
    else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)return false;
    s->ticks++;return true;
}
static bool output_submit(Output *o,Buffer *b,unsigned ticks) {
    if(o->buffer || b->locks)return false;
    b->refs++;b->locks++;o->buffer=b;o->hash=hash_bytes(b->data,b->length);o->ticks=ticks;return true;
}
static bool output_tick(Pool *p,Output *o) {
    if(!o->buffer)return true;
    if(o->hash!=hash_bytes(o->buffer->data,o->buffer->length))return false;
    if(--o->ticks==0){o->buffer->locks--;buffer_release(p,o->buffer);o->buffer=NULL;}
    return true;
}
static bool finish(Instance *i,Service *s) {
    while(i->status==READY || i->status==YIELDED){if(!service_tick(s))return false;resume(i,QUANTUM);}
    return i->status==DONE;
}
static uint32_t oracle(uint32_t seed,uint32_t frame) {
    uint32_t x=seed^(frame*0x9e3779b9u),h=2166136261u;
    for(uint32_t i=0;i<PIXELS;i++){x^=x<<13;x^=x>>17;x^=x<<5;x+=i+0x6d2b79f5u;for(unsigned c=0;c<3;c++){h^=(x>>(8*c))&255;h*=16777619u;}}
    return h;
}
static bool rejects(Memory *m,uint8_t *raw,size_t len) { Program *p=load_begin(m,raw,len);if(!p)return true;int status;do{status=load_step(p,4);}while(status==0);program_release(m,p);return status<0; }
static Program *tiny_program(Memory *m,uint8_t op,uint8_t a,uint8_t b,uint8_t c,uint32_t imm) {
    uint8_t raw[40]={0};memcpy(raw,"TVM1",4);put32(raw+4,1);put32(raw+8,1);put32(raw+20,STATES);put32(raw+24,STACK);put32(raw+28,1);
    raw[32]=op;raw[33]=a;raw[34]=b;raw[35]=c;put32(raw+36,imm);
    Program *p=load_begin(m,raw,sizeof(raw));if(p && load_step(p,1)!=1){program_release(m,p);return NULL;}return p;
}
int main(int argc,char **argv) {
    bool validate_only=argc==3 && strcmp(argv[1],"--validate")==0;
    if(argc!=2 && !validate_only){fprintf(stderr,"usage: typed-vm [--validate] package.tvm\n");return 2;}
    FILE *f=fopen(argv[validate_only?2:1],"rb");if(!f){perror("package");return 2;}
    if(fseek(f,0,SEEK_END)){fclose(f);return 2;}long file_len=ftell(f);rewind(f);
    if(file_len<0 || file_len>32+8*MAX_CODE){fclose(f);return 2;}
    size_t len=(size_t)file_len;uint8_t raw[32+8*MAX_CODE];
    if(fread(raw,1,len,f)!=len){fclose(f);return 2;}fclose(f);
    Memory mem={.limit=1024*1024,.fail_after=-1};Pool pool={.mem=&mem};Service svc={0};
    if(validate_only){bool invalid=rejects(&mem,raw,len);return invalid?2:0;}
    if(!service_open(&svc)){perror("socketpair");return 2;}
    uint64_t began=now_ns(),load_ns=0,init_ns=0,activation_ns=0,prep_max_ns=0;
    uint64_t t=now_ns();Program *program=load_begin(&mem,raw,len);load_ns+=now_ns()-t;
    if(!program){fprintf(stderr,"invalid artifact\n");close(svc.pair[0]);close(svc.pair[1]);return 2;}
    int load_status=0;bool service_ok=true;
    while(load_status==0){t=now_ns();load_status=load_step(program,4);uint64_t dt=now_ns()-t;load_ns+=dt;if(dt>prep_max_ns)prep_max_ns=dt;service_ok &= service_tick(&svc);}
    if(load_status<0){program_release(&mem,program);close(svc.pair[0]);close(svc.pair[1]);fprintf(stderr,"invalid instruction\n");return 2;}
    t=now_ns();Instance *a=instance_new(&pool,program,0x12345678u,PIXELS),*b=instance_new(&pool,program,0x87654321u,PIXELS);init_ns+=now_ns()-t;
    if(!a||!b){instance_free(a);instance_free(b);program_release(&mem,program);return 2;}
    uint64_t init_before=resume_ns;bool initialized=finish(a,&svc)&&finish(b,&svc);init_ns+=resume_ns-init_before;
    uint32_t hashes[3]={0};bool independent=initialized,resumable=true,nested_yield=false;
    uint64_t workload_start_ns=resume_ns,workload_start_calls=resume_calls;
    for(uint32_t frame=0;frame<3;frame++){
        frame_start(a,frame);frame_start(b,frame);unsigned slices=0;
        while(a->status==READY || a->status==YIELDED || b->status==READY || b->status==YIELDED){
            uint32_t before=a->executed;resume(a,QUANTUM);resume(b,QUANTUM);slices++;
            if(a->sp)nested_yield=true;
            if(a->executed-before>QUANTUM)resumable=false;
            service_ok &= service_tick(&svc);
        }
        hashes[frame]=hash_bytes(a->owned->data,a->owned->length);
        independent &= a->status==DONE && b->status==DONE && hashes[frame]==oracle(0x12345678u,frame) && hash_bytes(b->owned->data,b->owned->length)==oracle(0x87654321u,frame);
        resumable &= slices>1;
    }
    uint64_t workload_resume_ns=resume_ns-workload_start_ns,workload_resume_calls=resume_calls-workload_start_calls;
    check("independent_instances",independent && a->owned!=b->owned && a->program==b->program);
    check("resumable_execution",resumable && nested_yield);
    check("native_buffer_alias",resolve(&pool,a->views[0],0,a->owned->length,true)==a->owned->data);
    uint32_t previous=hash_bytes(a->owned->data,a->owned->length);
    bool bulk=bulk_invert(&pool,a->views[0]) && hash_bytes(a->owned->data,a->owned->length)!=previous && bulk_invert(&pool,a->views[0]) && hash_bytes(a->owned->data,a->owned->length)==previous;
    check("native_bulk_operation",bulk);

    /* An in-flight consumer keeps the old allocation immutable after activation. */
    Output output={0};Buffer *old_buffer=a->owned;View old_view=a->views[0];
    bool retention=output_submit(&output,old_buffer,8) && !bulk_invert(&pool,old_view);
    Instance write_attempt=*a;frame_start(&write_attempt,8);
    bool blocked_write=!finish(&write_attempt,&svc) && write_attempt.status==FAULT;
    check("inflight_write_rejected",blocked_write && hash_bytes(old_buffer->data,old_buffer->length)==previous);
    /* Rotate rather than copy: the old effect remains runnable while the output
     * owns its last completed frame. Both allocations count toward admission. */
    Buffer *next_buffer=buffer_new(&pool,PIXELS*3);
    if(!next_buffer){fprintf(stderr,"buffer rotation allocation failed\n");return 2;}
    buffer_release(&pool,a->owned);a->owned=next_buffer;a->views[0]=view_of(next_buffer);frame_start(a,8);
    uint32_t old_executed_before=a->executed;
    uint64_t service_before=svc.received;uint32_t other_before=b->executed;
    /* Build a second serialized artifact with different initialization code.
     * Its explicit CONST makes the changed behavior independent of host inputs. */
    uint32_t original_count=u32(raw+8),original_init=u32(raw+12);
    if(original_count>MAX_CODE-4){fprintf(stderr,"no replacement package headroom\n");return 2;}
    memset(raw+len,0,32);raw[len]=CONST;put32(raw+len+4,0x11223344u);
    raw[len+8]=STATE_STORE;raw[len+16]=STATE_STORE;raw[len+17]=2;put32(raw+len+20,1);
    put32(raw+8,original_count+4);put32(raw+12,original_count);
    frame_start(b,7);t=now_ns();Program *replacement_code=load_begin(&mem,raw,len+32);load_ns+=now_ns()-t;
    put32(raw+8,original_count);put32(raw+12,original_init);
    bool replacement=replacement_code!=NULL;
    if(replacement){load_status=0;while(load_status==0){t=now_ns();load_status=load_step(replacement_code,4);uint64_t dt=now_ns()-t;load_ns+=dt;if(dt>prep_max_ns)prep_max_ns=dt;resume(a,QUANTUM);resume(b,QUANTUM);service_ok &= service_tick(&svc);}replacement=load_status==1;}
    t=now_ns();Instance *replacement_instance=replacement?instance_new(&pool,replacement_code,0,PIXELS):NULL;init_ns+=now_ns()-t;
    if(replacement_instance){while(replacement_instance->status==READY || replacement_instance->status==YIELDED){t=now_ns();resume(replacement_instance,1);init_ns+=now_ns()-t;resume(a,QUANTUM);resume(b,QUANTUM);service_ok &= service_tick(&svc);}}
    replacement &= replacement_instance && replacement_instance->status==DONE;
    bool old_progress=a->executed>old_executed_before;
    while(a->status==READY || a->status==YIELDED){resume(a,QUANTUM);resume(b,QUANTUM);service_ok &= service_tick(&svc);}
    check("old_effect_runs_during_preparation",old_progress && a->status==DONE && hash_bytes(a->owned->data,a->owned->length)==oracle(0x12345678u,8));
    size_t replacement_peak=mem.live;
    if(replacement){t=now_ns();Instance *old=a;a=replacement_instance;instance_free(old);activation_ns=now_ns()-t;}
    else instance_free(replacement_instance);
    program_release(&mem,replacement_code);
    retention &= pool.slots[old_view.slot]==old_buffer && old_buffer->refs==1 && old_buffer->locks==1;
    if(replacement){frame_start(a,9);finish(a,&svc);replacement &= hash_bytes(a->owned->data,a->owned->length)==oracle(0x11223344u,9);}
    check("replacement",replacement && b->executed>other_before && svc.received>service_before);
    for(unsigned tick=0;tick<8;tick++){retention &= output_tick(&pool,&output);service_ok &= service_tick(&svc);if(b->status!=DONE)resume(b,QUANTUM);}
    check("async_output_retention",retention && output.buffer==NULL && pool.slots[old_view.slot]==NULL);
    check("stale_view_rejection",resolve(&pool,old_view,0,1,false)==NULL);
    Buffer *reused=buffer_new(&pool,12);check("generation_reuse_rejected",reused && reused->slot==old_view.slot && !resolve(&pool,old_view,0,1,false));buffer_release(&pool,reused);
    Pool exhausted={.mem=&mem};for(unsigned slot=0;slot<SLOTS;slot++)exhausted.generations[slot]=UINT32_MAX;
    check("generation_wrap_rejected",buffer_new(&exhausted,1)==NULL);

    /* Validation failures and both allocation failure sites preserve a live effect. */
    uint8_t saved=raw[32];raw[32]=255;size_t live_before=mem.live;
    bool malformed=rejects(&mem,raw,len);raw[32]=saved;
    saved=raw[33];raw[33]=REGS;malformed &= rejects(&mem,raw,len);raw[33]=saved;
    uint32_t saved_header=u32(raw+28);put32(raw+28,2);malformed &= rejects(&mem,raw,len);put32(raw+28,saved_header);
    saved=raw[0];raw[0]='X';malformed &= rejects(&mem,raw,len);raw[0]=saved;
    malformed &= rejects(&mem,raw,len-1);
    uint32_t saved_count=u32(raw+8);put32(raw+8,UINT32_MAX);malformed &= rejects(&mem,raw,len);put32(raw+8,saved_count);
    check("malformed_artifact_rejected",malformed && mem.live==live_before);
    mem.limit=mem.live;Instance *bad=instance_new(&pool,program,1,PIXELS);bool admission=bad==NULL;instance_free(bad);mem.limit=1024*1024;
    for(int site=0;site<2;site++){mem.fail_after=site;bad=instance_new(&pool,program,1,PIXELS);admission &= bad==NULL;instance_free(bad);admission &= mem.live==live_before;}mem.fail_after=-1;
    check("admission_failure",admission);
    frame_start(a,10);bool rollback=finish(a,&svc) && hash_bytes(a->owned->data,a->owned->length)==oracle(0x11223344u,10);
    check("failed_replacement_preserves_old",rollback && malformed && admission);

    /* Execute malicious-but-valid programs through the same VM dispatch. */
    Program *evil=tiny_program(&mem,STORE8,0,1,0,0);Instance *victim=instance_new(&pool,evil,PIXELS*3,PIXELS);resume(victim,QUANTUM);
    bool bounds=victim->status==FAULT && !resolve(&pool,victim->views[0],UINT32_MAX,2,true);instance_free(victim);program_release(&mem,evil);
    evil=tiny_program(&mem,STORE8,0,1,0,1);victim=instance_new(&pool,evil,UINT32_MAX,PIXELS);resume(victim,QUANTUM);bounds &= victim->status==FAULT;instance_free(victim);program_release(&mem,evil);
    check("bounds_rejection",bounds);
    evil=tiny_program(&mem,STORE8,0,1,0,0);victim=instance_new(&pool,evil,0,PIXELS);victim->views[0]=old_view;resume(victim,QUANTUM);check("guest_stale_view_fault",victim->status==FAULT);instance_free(victim);program_release(&mem,evil);
    evil=tiny_program(&mem,JMP,0,0,0,0);victim=instance_new(&pool,evil,1,PIXELS);unsigned runaway_slices=0;
    while(victim->status==READY || victim->status==YIELDED){resume(victim,QUANTUM);runaway_slices++;service_ok &= service_tick(&svc);}
    check("runaway_bounded",victim->status==FAULT && victim->executed==JOB_BUDGET && runaway_slices>1);
    instance_free(victim);program_release(&mem,evil);
    evil=tiny_program(&mem,CALL,0,0,0,0);victim=instance_new(&pool,evil,1,PIXELS);resume(victim,QUANTUM);check("call_stack_overflow_rejected",victim->status==FAULT && victim->sp==STACK);instance_free(victim);program_release(&mem,evil);
    evil=tiny_program(&mem,RET,0,0,0,0);victim=instance_new(&pool,evil,1,PIXELS);resume(victim,QUANTUM);check("call_stack_underflow_rejected",victim->status==FAULT);instance_free(victim);program_release(&mem,evil);
    frame_start(a,11);resume(a,7);uint32_t pc=a->pc;a->cancel=true;resume(a,7);check("cancellation",a->status==CANCELLED && a->pc==pc);
    check("service_progress",service_ok && svc.received==svc.sent && svc.received>100);
    instance_free(a);instance_free(b);program_release(&mem,program);close(svc.pair[0]);close(svc.pair[1]);
    bool clean=mem.live==0;for(unsigned i=0;i<SLOTS;i++)clean &= pool.slots[i]==NULL;check("cleanup",clean);
    bool passed=true;for(unsigned i=0;i<nchecks;i++)passed &= checks[i].passed;
    printf("{\"status\":\"%s\",\"workload\":{\"pixels\":%u,\"seed\":305419896,\"frame_hashes\":[\"%08" PRIx32 "\",\"%08" PRIx32 "\",\"%08" PRIx32 "\"]},\"checks\":{",passed?"passed":"failed",PIXELS,hashes[0],hashes[1],hashes[2]);
    for(unsigned i=0;i<nchecks;i++)printf("%s\"%s\":%s",i?",":"",checks[i].name,checks[i].passed?"true":"false");
    printf("},\"metrics\":{\"timing_unit\":\"ns\",\"workload_resume_wall_ns\":%" PRIu64 ",\"workload_resume_calls\":%" PRIu64 ",\"workload_effect_frames\":6,\"resume_calls\":%" PRIu64 ",\"resume_max_observed_ns\":%" PRIu64 ",\"load_validation_wall_ns\":%" PRIu64 ",\"initialization_wall_ns\":%" PRIu64 ",\"activation_wall_ns\":%" PRIu64 ",\"validation_slice_max_observed_ns\":%" PRIu64 ",\"harness_wall_ns\":%" PRIu64 ",\"tracked_component_peak_bytes\":%zu,\"replacement_coexistence_bytes\":%zu,\"tracked_component_final_bytes\":%zu,\"largest_tracked_allocation_bytes\":%zu,\"tracked_allocations\":%zu,\"instance_metadata_bytes\":%zu,\"buffer_metadata_bytes\":%zu,\"artifact_bytes\":%zu,\"input_staging_stack_bytes\":32800,\"rgb_buffer_bytes\":900,\"replacement_peak_rgb_buffers\":4,\"retained_output_buffers\":1,\"pixel_data_copies\":0,\"instruction_quantum\":%u,\"job_instruction_limit\":%u,\"service_messages\":%" PRIu64 ",\"service_ticks\":%" PRIu64 "}}\n",workload_resume_ns,workload_resume_calls,resume_calls,resume_max_ns,load_ns,init_ns,activation_ns,prep_max_ns,now_ns()-began,mem.peak,replacement_peak,mem.live,mem.largest,mem.allocations,sizeof(Instance),sizeof(Buffer),len,QUANTUM,JOB_BUDGET,svc.received,svc.ticks);
    return passed?0:1;
}
#endif
