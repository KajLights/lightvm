#ifndef DEMO_PROTOCOL_H
#define DEMO_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

#define FRAME_LIMIT 512u
#define HEADER_SIZE 16u
#define LIGHTVM_PROTOCOL_VERSION 1u
enum Message { CMD_STATUS=1, CMD_BEGIN=2, CMD_CHUNK=3, CMD_FINISH=4,
               CMD_RUN=5, CMD_RESET=6, CMD_PLAY=7, CMD_STOP=8,
               CMD_FRAME=9, // Command 10 is reserved; keep management IDs stable.
               CMD_MANAGE=11, CMD_OBJECT_BEGIN=12, CMD_OBJECT_CHUNK=13,
               CMD_OBJECT_FINISH=14, CMD_OBJECT_CANCEL=15,
               RESP_OUTPUT=128, RESP_STATUS=129, RESP_PIXELS=130, RESP_DATA=131 };
enum Status { STATUS_OK=0, STATUS_PROTOCOL=1, STATUS_UPLOAD=2,
              STATUS_LOAD=3, STATUS_RUN=4, STATUS_LIMIT=5, STATUS_EXIT=6,
              STATUS_CONFIG=7, STATUS_STORAGE=8, STATUS_BUSY=9 };
typedef void (*SendMessage)(void *, uint8_t, uint16_t, const uint8_t *, size_t);
typedef void (*ReceiveMessage)(void *, uint8_t, uint16_t, const uint8_t *, size_t);
typedef void (*WriteBytes)(void *, const uint8_t *, size_t);
typedef struct {
    uint8_t header[HEADER_SIZE], payload[FRAME_LIMIT];
    size_t header_used, payload_used, payload_size;
    ReceiveMessage receive;
    SendMessage send;
    void *context;
} Decoder;

uint32_t read_u32(const uint8_t *p);
void write_u32(uint8_t *p, uint32_t value);
uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t size);
uint32_t crc32_bytes(const uint8_t *p, size_t size);
void send_status(SendMessage send, void *context, uint16_t id,
                 enum Status code, int32_t value, const char *message);
void write_frame(WriteBytes write, void *context, uint8_t type, uint16_t id,
                 const uint8_t *payload, size_t size);
void decoder_init(Decoder *decoder, ReceiveMessage receive, SendMessage send, void *context);
void decoder_feed(Decoder *decoder, uint8_t byte);
#endif
