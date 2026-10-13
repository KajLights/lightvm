// SPDX-License-Identifier: MIT
// Copyright (c) 2018-2019 Damien P. George
// Copyright (c) 2026 LightVM contributors
// Adapted from MicroPython's DHCP server in Raspberry Pi pico-examples:
// https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/access_point/dhcpserver
// The original MIT notice is retained in licenses/dhcp-server.txt.
#include "dhcp.h"
#include <stdbool.h>
#include <string.h>

static const uint8_t server_ip[]={192,168,4,1},mask[]={255,255,255,0};
static const uint8_t cookie[]={99,130,83,99};
static const uint8_t empty_mac[6]={0};
static void option(uint8_t *out,size_t *used,uint8_t type,const uint8_t *value,uint8_t size) {
    out[(*used)++]=type; out[(*used)++]=size;
    memcpy(out+*used,value,size); *used+=size;
}
size_t dhcp_reply(DhcpServer *s,const uint8_t *in,size_t size,uint8_t *out,size_t capacity,uint64_t now) {
    if (size<240 || size>DHCP_PACKET_LIMIT || capacity<300 || in[0]!=1 || in[1]!=1 || in[2]!=6 || in[3] ||
        memcmp(in+236,cookie,4) || !memcmp(in+28,empty_mac,6) || (in[28]&1)) return 0;
    const uint8_t *type=NULL,*requested=NULL,*server=NULL;
    bool ended=false;
    for (size_t i=240;i<size;) {
        unsigned code=in[i++];
        if (!code) continue;
        if (code==255) { ended=true; break; }
        if (i==size) return 0;
        unsigned length=in[i++]; if (length>size-i) return 0;
        if (code==53) { if (type || length!=1) return 0; type=in+i; }
        else if (code==50) { if (requested || length!=4) return 0; requested=in+i; }
        else if (code==54) { if (server || length!=4) return 0; server=in+i; }
        i+=length;
    }
    if (!ended || !type || (server && memcmp(server,server_ip,4))) return 0;
    unsigned index=DHCP_CLIENT_LIMIT; uint8_t response=0;
    if (*type==1) {
        unsigned free_index=DHCP_CLIENT_LIMIT;
        for (unsigned i=0;i<DHCP_CLIENT_LIMIT;i++) {
            DhcpLease *l=&s->leases[i];
            if (l->expires>now && !memcmp(l->mac,in+28,6)) { index=i; break; }
            if (free_index==DHCP_CLIENT_LIMIT && l->expires<=now) free_index=i;
        }
        if (index==DHCP_CLIENT_LIMIT) index=free_index;
        if (index==DHCP_CLIENT_LIMIT) return 0;
        DhcpLease *l=&s->leases[index]; memcpy(l->mac,in+28,6);
        if (l->expires<=now) l->expires=now+60000000u; // Reserve an offer for one minute.
        response=2;
    } else if (*type==3 || *type==4 || *type==7) {
        const uint8_t *address=requested?requested:in+12; // ciaddr is used for renewal/release.
        if (!memcmp(address,server_ip,3) && address[3]>=2 && address[3]<2+DHCP_CLIENT_LIMIT) index=address[3]-2;
        if (*type!=3) {
            if (index<DHCP_CLIENT_LIMIT && !memcmp(s->leases[index].mac,in+28,6)) memset(&s->leases[index],0,sizeof(DhcpLease));
            return 0;
        }
        response=6;
        if (index<DHCP_CLIENT_LIMIT) {
            DhcpLease *l=&s->leases[index];
            if (l->expires<=now || !memcmp(l->mac,in+28,6)) {
                memcpy(l->mac,in+28,6); l->expires=now+86400000000ull; response=5;
            }
        }
    } else return 0;
    memset(out,0,300); out[0]=2; out[1]=1; out[2]=6;
    memcpy(out+4,in+4,4); memcpy(out+10,in+10,2); memcpy(out+28,in+28,16);
    if (response==5) memcpy(out+12,in+12,4);
    if (response!=6) { memcpy(out+16,server_ip,4); out[19]=(uint8_t)(index+2); }
    memcpy(out+20,server_ip,4); memcpy(out+236,cookie,4);
    size_t used=240;
    option(out,&used,53,&response,1); option(out,&used,54,server_ip,4);
    if (response!=6) {
        static const uint8_t duration[]={0,1,81,128}; // 86400 seconds, network byte order.
        option(out,&used,51,duration,4); option(out,&used,1,mask,4); option(out,&used,3,server_ip,4);
    }
    out[used]=255; return 300;
}
