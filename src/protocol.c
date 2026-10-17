#include "protocol.h"
#include <string.h>

static const uint8_t magic[4] = {'L','V','M','1'};
uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
void write_u32(uint8_t *p, uint32_t v) {
    for (unsigned i=0; i<4; ++i) p[i]=(uint8_t)(v>>(8*i));
}
uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t size) {
    while (size--) {
        crc ^= *p++;
        for (unsigned bit=0; bit<8; ++bit)
            crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
    }
    return crc;
}
uint32_t crc32_bytes(const uint8_t *p, size_t size) {
    return crc32_update(0xffffffffu,p,size)^0xffffffffu;
}
void send_status(SendMessage send, void *context, uint16_t id,
                 enum Status code, int32_t value, const char *message) {
    uint8_t payload[FRAME_LIMIT];
    size_t length=strlen(message);
    if (length>FRAME_LIMIT-8) length=FRAME_LIMIT-8;
    write_u32(payload,(uint32_t)code);
    write_u32(payload+4,(uint32_t)value);
    memcpy(payload+8,message,length);
    send(context,RESP_STATUS,id,payload,length+8);
}
void write_frame(WriteBytes write, void *context, uint8_t type, uint16_t id,
                 const uint8_t *payload, size_t size) {
    uint8_t header[HEADER_SIZE]={'L','V','M','1',LIGHTVM_PROTOCOL_VERSION,type,(uint8_t)id,(uint8_t)(id>>8)};
    write_u32(header+8,(uint32_t)size);
    uint32_t crc=crc32_update(0xffffffffu,header+4,8);
    crc=crc32_update(crc,payload,size)^0xffffffffu;
    write_u32(header+12,crc);
    write(context,header,sizeof(header));
    if (size) write(context,payload,size);
}
void decoder_init(Decoder *d, ReceiveMessage receive, SendMessage send, void *context) {
    memset(d,0,sizeof(*d)); d->receive=receive; d->send=send; d->context=context;
}
static void finish_frame(Decoder *d) {
    uint16_t id=(uint16_t)(d->header[6] | (uint16_t)d->header[7]<<8);
    uint32_t crc=crc32_update(0xffffffffu,d->header+4,8);
    crc=crc32_update(crc,d->payload,d->payload_size)^0xffffffffu;
    if (crc==read_u32(d->header+12))
        d->receive(d->context,d->header[5],id,d->payload,d->payload_size);
    else send_status(d->send,d->context,id,STATUS_PROTOCOL,0,"frame checksum mismatch");
    d->header_used=0; d->payload_used=0;
}
void decoder_feed(Decoder *d, uint8_t byte) {
    if (d->header_used<4) {
        if (byte==magic[d->header_used]) d->header[d->header_used++]=byte;
        else d->header_used=(byte==magic[0]) ? 1 : 0;
        return;
    }
    if (d->header_used<HEADER_SIZE) {
        d->header[d->header_used++]=byte;
        if (d->header_used==HEADER_SIZE) {
            d->payload_size=read_u32(d->header+8);
            if (d->header[4]!=LIGHTVM_PROTOCOL_VERSION || d->payload_size>FRAME_LIMIT) {
                uint16_t id=(uint16_t)(d->header[6] | (uint16_t)d->header[7]<<8);
                send_status(d->send,d->context,id,STATUS_PROTOCOL,0,"invalid frame header");
                d->header_used=0;
            } else if (!d->payload_size) finish_frame(d);
        }
        return;
    }
    d->payload[d->payload_used++]=byte;
    if (d->payload_used==d->payload_size) finish_frame(d);
}
