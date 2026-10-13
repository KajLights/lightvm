#include "dhcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static size_t packet(uint8_t *p,uint8_t mac,uint8_t type,uint8_t address) {
    memset(p,0,DHCP_PACKET_LIMIT); p[0]=1; p[1]=1; p[2]=6; p[4]=42; p[10]=128;
    p[28]=2; p[33]=mac; memcpy(p+236,"\x63\x82\x53\x63",4);
    p[240]=0; p[241]=53; p[242]=1; p[243]=type;
    size_t size=244;
    if (address) { p[size++]=50; p[size++]=4; p[size++]=192; p[size++]=168; p[size++]=4; p[size++]=address; }
    p[size++]=255; return size;
}
int main(void) {
    DhcpServer s={0}; uint8_t in[DHCP_PACKET_LIMIT],out[300];
    size_t size=packet(in,1,1,0);
    for (size_t i=0;i<size;i++) CHECK(!dhcp_reply(&s,in,i,out,sizeof(out),1));
    CHECK(!dhcp_reply(&s,in,size,out,299,1));
    CHECK(!dhcp_reply(&s,in,DHCP_PACKET_LIMIT+1,out,sizeof(out),1));
    CHECK(dhcp_reply(&s,in,size,out,sizeof(out),1)==300 && out[242]==2 && out[19]==2 && out[4]==42 && out[10]==128);
    size=packet(in,1,3,2);
    CHECK(dhcp_reply(&s,in,size,out,sizeof(out),2)==300 && out[242]==5);
    size=packet(in,1,3,0); memcpy(in+12,"\xc0\xa8\x04\x02",4);
    CHECK(dhcp_reply(&s,in,size,out,sizeof(out),3)==300 && out[242]==5 && !memcmp(out+12,in+12,4)); // Renewal via ciaddr.
    size=packet(in,2,3,2);
    CHECK(dhcp_reply(&s,in,size,out,sizeof(out),4)==300 && out[242]==6 && !out[19]); // Conflicting lease.
    for (uint8_t mac=2;mac<=4;mac++) {
        size=packet(in,mac,1,0); CHECK(dhcp_reply(&s,in,size,out,sizeof(out),5)==300 && out[19]==mac+1);
    }
    size=packet(in,5,1,0); CHECK(!dhcp_reply(&s,in,size,out,sizeof(out),6));
    size=packet(in,1,7,0); memcpy(in+12,"\xc0\xa8\x04\x02",4);
    CHECK(!dhcp_reply(&s,in,size,out,sizeof(out),7) && !s.leases[0].expires);
    size=packet(in,5,1,0); CHECK(dhcp_reply(&s,in,size,out,sizeof(out),8)==300 && out[19]==2);
    size=packet(in,6,1,0); CHECK(dhcp_reply(&s,in,size,out,sizeof(out),60000009)==300); // Expired offers.
    size=packet(in,6,3,100); CHECK(dhcp_reply(&s,in,size,out,sizeof(out),60000010)==300 && out[242]==6);
    size=packet(in,7,1,0); in[244]=54; in[245]=4; memcpy(in+246,"\xc0\xa8\x04\x09",4); in[250]=255;
    CHECK(!dhcp_reply(&s,in,251,out,sizeof(out),60000011)); // Another DHCP server.
    size=packet(in,7,1,0); in[242]=0; CHECK(!dhcp_reply(&s,in,size,out,sizeof(out),1));
    size=packet(in,7,1,0); in[244]=54; in[245]=4; CHECK(!dhcp_reply(&s,in,247,out,sizeof(out),1));
    size=packet(in,7,1,0); in[236]=0; CHECK(!dhcp_reply(&s,in,size,out,sizeof(out),1));
    size=packet(in,7,1,0); in[2]=16; CHECK(!dhcp_reply(&s,in,size,out,sizeof(out),1));
    size=packet(in,7,1,0); in[244]=53; in[245]=1; in[246]=3; in[247]=255;
    CHECK(!dhcp_reply(&s,in,248,out,sizeof(out),1)); // Duplicate message type.
    // Probe arbitrary option bytes with deterministic inputs and fresh state.
    uint32_t random=7;
    for (unsigned i=0;i<10000;i++) {
        packet(in,8,1,0); memset(&s,0,sizeof(s));
        for (unsigned j=240;j<DHCP_PACKET_LIMIT;j++) { random=random*1664525u+1013904223u; in[j]=(uint8_t)(random>>24); }
        dhcp_reply(&s,in,240+i%(DHCP_PACKET_LIMIT-239),out,sizeof(out),i);
    }
    return 0;
}
