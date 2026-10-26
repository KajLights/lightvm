#ifndef LIGHTVM_AUDIO_RECEIVER_H
#define LIGHTVM_AUDIO_RECEIVER_H
#include "inputs.h"
#include <stddef.h>

#define AUDIO_PACKET_LIMIT 692u
#define AUDIO_FEATURES 6u
#define AUDIO_SPECTRUM_INPUTS 16u
#define AUDIO_SOURCES (7u+AUDIO_SPECTRUM_INPUTS)
#define AUDIO_TIMEOUT_US 1000000u
#define AUDIO_EVENT_TTL_US 350000u
typedef struct {
    uint64_t sensor;
    uint8_t group[4];
    uint16_t port;
    bool allow_synthetic;
} AudioConfig;
typedef struct {
    uint64_t id,time;
    float strength;
} AudioEvent;
typedef struct {
    uint64_t sensor,session,sequence,audio,send;
    float values[AUDIO_FEATURES];
    float spectrum[AUDIO_SPECTRUM_INPUTS];
    uint16_t edges[65];
    AudioEvent events[4];
    uint8_t flags,bands,event_count;
} AudioFrame;
typedef struct AudioReceiver {
    AudioConfig config;
    uint64_t session,sequence,audio,send,received,retired[4];
    uint64_t event_high,event_seen;
    uint16_t edges[65];
    uint8_t flags,bands,retire_next;
    InputValue values[AUDIO_SOURCES];
    uint32_t generation,publication;
    uint32_t packets,published,invalid,ignored,reordered,gaps,coalesced;
    uint32_t onsets,duplicates,expired,overflow,max_service_us;
    bool pending,listening,conflict;
} AudioReceiver;

bool audio_decode(const uint8_t *,size_t,AudioFrame *);
/* Parse an optional config object; returns a bounded allocation or NULL. */
bool audio_config_parse(const Json *,int,AudioReceiver **,char *,size_t);
const char *audio_receiver_claim(AudioReceiver *,InputRegistry *);
void audio_receiver_receive(AudioReceiver *,const uint8_t *,size_t,uint64_t);
void audio_receiver_publish(AudioReceiver *,InputRegistry *,uint64_t);
void audio_receiver_disconnect(AudioReceiver *);
#endif
