#ifndef LIGHTVM_AUDIO_SOURCES_H
#define LIGHTVM_AUDIO_SOURCES_H
#include "inputs.h"
#include "subscriber.h"

#define AUDIO_PROVIDER_LIMIT 4u
#define AUDIO_ALIAS_LIMIT 23u
enum { AUDIO_UDP=1,AUDIO_GATEWAY=2 };
/* LightVM-specific command; the shared device protocol keeps its 512-byte limit.
 * LE generation:u32, frame_id:u64, total:u16, offset:u16, then frame bytes. */
#define CMD_AUDIO_FRAME 16u
#define AUDIO_FRAGMENT_HEADER 16u
typedef struct AudioGateway {
    KfSubscriber receiver;
    uint64_t owner,deadline,challenge,challenge_sent,frame_id,frame_deadline;
    uint32_t generation;
    uint16_t staged,total;
    bool synthetic;
    uint8_t staging[KF_WIRE_MAX];
} AudioGateway;
typedef struct {
    char address[128]; /* normalized DNS/IPv4 address, or gateway provider name */
    uint64_t pin;
    uint32_t profile;
    uint16_t port;
    uint8_t type;
    bool synthetic;
} AudioSourceConfig;
typedef struct AudioProvider {
    AudioSourceConfig config;
    KfSubscriber receiver;
    void *native;
    AudioGateway *gateway; /* Shared ingress; canonical scoped policies remain distinct. */
    unsigned references;
    uint64_t retry_at;
    const char *error;
} AudioProvider;
typedef struct {
    bool (*open)(void *,AudioProvider *);
    void (*close)(void *,AudioProvider *);
    bool (*send)(void *,AudioProvider *,const uint8_t *,size_t);
    void *context;
    size_t charged_bytes;
} AudioTransport;
typedef struct AudioSources {
    AudioProvider *providers[AUDIO_PROVIDER_LIMIT];
    AudioTransport transport;
    uint32_t generation;
} AudioSources;
typedef struct {
    char alias[AUDIO_ALIAS_LIMIT+1];
    AudioSourceConfig config;
    AudioProvider *provider;
} InputSourceDeclaration;
typedef struct InputSourceScope {
    AudioSources *manager;
    unsigned references,demand,count;
    InputSourceDeclaration declarations[];
} InputSourceScope;
/* A foreground command can reserve the union of several retained scene scopes
 * before changing playback. Config pointers stay valid until commit/release. */
typedef struct {
    AudioSources *manager;
    const AudioSourceConfig *configs[AUDIO_PROVIDER_LIMIT];
    AudioProvider *providers[AUDIO_PROVIDER_LIMIT];
    unsigned count,acquired;
} AudioSourcePlan;
const char *audio_source_plan_add(AudioSourcePlan *,const InputSourceScope *);
const char *audio_source_plan_acquire(AudioSourcePlan *);
void audio_source_plan_commit(AudioSourcePlan *);
void audio_source_plan_release(AudioSourcePlan *);

const char *input_scope_parse(AudioSources *,const Json *,int,InputSourceScope **);
void input_scope_retain(InputSourceScope *);
void input_scope_release(InputSourceScope *);
const char *input_scope_demand(InputSourceScope *,bool);
void input_scope_describe(const InputSourceScope *,void (*)(void *,const char *),void *);
/* Declared aliases shadow the global registry even while unavailable. */
bool input_scope_lookup(const InputSourceScope *,const char *,InputSignal *,uint64_t *,uint64_t now);
void audio_sources_tick(AudioSources *,uint64_t);
void audio_sources_disconnect(AudioSources *);
void audio_sources_destroy(AudioSources *);
void audio_source_receive(AudioProvider *,const uint8_t *,size_t,uint64_t);
void audio_sources_status(const AudioSources *,void (*)(void *,const char *),void *);
uint64_t audio_source_random(void);
typedef struct { uint32_t generation;uint64_t challenge,sent; } AudioChallenge;
const char *audio_gateway_claim(AudioSources *,const Json *,uint64_t owner,uint64_t now,AudioChallenge *);
const char *audio_gateway_keep(AudioSources *,uint64_t owner,uint32_t generation,uint64_t now,AudioChallenge *);
const char *audio_gateway_clock(AudioSources *,uint64_t owner,uint32_t generation,uint64_t challenge,uint64_t remote,uint64_t now);
const char *audio_gateway_fragment(AudioSources *,uint64_t owner,const uint8_t *,size_t,uint64_t now);
const char *audio_gateway_release(AudioSources *,uint64_t owner,uint32_t generation);
void audio_gateway_disconnect(AudioSources *,uint64_t owner);
bool audio_source_id(const Json *,int,uint64_t *);
#endif
