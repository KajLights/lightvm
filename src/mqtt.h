#ifndef LIGHTVM_MQTT_H
#define LIGHTVM_MQTT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Broker settings are independent of both LightVM and Home Assistant. Brokers
 * use IPv4 addresses or DNS hostnames and unencrypted MQTT 3.1.1. */
#define MQTT_HOST_CAPACITY 128u
typedef struct {
    bool enabled;
    uint16_t port;
    char host[MQTT_HOST_CAPACITY], username[65], password[65];
} MqttConfig;

/* Nonblocking byte stream. open starts connecting; poll returns -1=failed,
 * 0=pending, 1=connected. read/write return -1=closed, 0=would block, >0=bytes.
 * Only the engine owner calls these functions or the message callback. */
typedef struct {
    void *context;
    bool (*open)(void *, const char *, uint16_t);
    int (*poll)(void *);
    int (*read)(void *, uint8_t *, size_t);
    int (*write)(void *, const uint8_t *, size_t);
    void (*close)(void *);
} MqttStream;
typedef void (*MqttMessage)(void *, const char *, const uint8_t *, size_t, bool retained);
#define MQTT_BUFFER_SIZE 2048u
#define MQTT_TOPIC_SIZE 192u
typedef enum { MQTT_DOWN, MQTT_TCP, MQTT_CONNACK, MQTT_ONLINE } MqttState;
typedef struct {
    MqttStream stream;
    MqttMessage message;
    void *context;
    MqttConfig config;
    MqttState state;
    char client_id[96], will_topic[MQTT_TOPIC_SIZE], error[96];
    uint8_t rx[MQTT_BUFFER_SIZE], tx[MQTT_BUFFER_SIZE];
    size_t received, queued, sent;
    uint64_t retry_at, deadline, tx_progress, last_sent, ping_deadline;
    unsigned failures;
    uint32_t generation;
    uint16_t sequence, subscription;
} Mqtt;
void mqtt_init(Mqtt *, MqttStream, const char *client_id, const char *will_topic, MqttMessage, void *);
void mqtt_close(Mqtt *);
void mqtt_tick(Mqtt *, const MqttConfig *, bool network_ready, uint64_t now);
bool mqtt_publish(Mqtt *, const char *topic, const void *, size_t, bool retained);
/* One subscription may be outstanding; rejection disconnects the session. */
bool mqtt_subscribe(Mqtt *, const char *filter);
#endif
