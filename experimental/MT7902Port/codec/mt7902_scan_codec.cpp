// SPDX-License-Identifier: BSD-3-Clause-Clear
// Reference: mt76_connac_mcu.{c,h}, mt7921/mcu.c (MediaTek Inc.).
#include "mt7902_scan_codec.h"
#include <string.h>
namespace {
uint16_t le16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8); }
uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
void put32(uint8_t *p,uint32_t v) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8)); }
const uint8_t channels[][2]={{1,1},{1,2},{1,3},{1,4},{1,5},{1,6},{1,7},{1,8},{1,9},{1,10},{1,11},{1,12},{1,13},
    {2,36},{2,40},{2,44},{2,48},{2,149},{2,153},{2,157},{2,161},{2,165}};
bool valid(const Mt7902NicCaps &c) { return c.nss>=1 && c.nss<=4 && (c.bands&3) && !(c.bands&~3u); }
bool selected(uint8_t band,const Mt7902NicCaps &c) { return (c.bands&(1u<<(band-1)))!=0; }
}
bool mt7902_decode_nic_caps(const uint8_t *p,size_t n,Mt7902NicCaps *out)
{
    if (!p || !out || n<4) return false;
    const uint16_t count=le16(p);
    if (!count || count>(n-4)/8) return false;
    Mt7902NicCaps caps={}; bool mac=false,phy=false;
    size_t offset=4;
    for (uint16_t i=0;i<count;i++) {
        if (offset>n || n-offset<8) return false;
        uint32_t tag=le32(p+offset),length=le32(p+offset+4); offset+=8;
        if (length>n-offset) return false;
        const uint8_t *v=p+offset;
        if (tag==7) {
            if (mac || length<6 || (v[0]&1) || !(v[0]|v[1]|v[2]|v[3]|v[4]|v[5])) return false;
            memcpy(caps.mac,v,6); mac=true;
        } else if (tag==8) {
            if (phy || length<12 || !v[4] || v[4]>4) return false;
            caps.ht=v[0]; caps.vht=v[1]; caps.max_bw=v[3]; caps.nss=v[4];
            caps.chain_mask=(uint8_t)((1u<<caps.nss)-1); caps.bands=v[10]&3; caps.he=v[11]; phy=true;
        }
        offset+=length;
    }
    if (!mac || !phy || !valid(caps) || n-offset>3) return false;
    while (offset<n) if (p[offset++]) return false;
    *out=caps;
    return true;
}
size_t mt7902_encode_passive_scan(uint8_t *out,size_t capacity,uint8_t seq,const Mt7902NicCaps &caps)
{
    if (!out || capacity<MT7902_PASSIVE_SCAN_BYTES || !seq || seq>127 || !valid(caps)) return 0;
    memset(out,0,MT7902_PASSIVE_SCAN_BYTES);
    out[0]=seq; out[3]=1; out[6]=1u<<5; out[7]=1; // wildcard, split scan, v1; scan_type/probe count stay zero.
    unsigned count=0;
    for (const auto &c:channels) if (selected(c[0],caps)) {
        out[160+count*2]=c[0]; out[161+count*2]=c[1]; count++;
    }
    put16(out+154,120); put16(out+156,(uint16_t)(120*count));
    out[158]=4; out[159]=(uint8_t)count;
    put16(out+828,60); memset(out+1110,255,6);
    return MT7902_PASSIVE_SCAN_BYTES;
}
size_t mt7902_encode_scan_domain(uint8_t *out,size_t capacity,const Mt7902NicCaps &caps)
{
    if (!out || !valid(caps)) return 0;
    unsigned count=0;
    for (const auto &c:channels) if (selected(c[0],caps)) count++;
    const size_t size=12+8*count;
    if (capacity<size) return 0;
    memset(out,0,size); out[0]='0'; out[1]='0'; out[4]=out[5]=out[6]=1; // BW20 only.
    size_t offset=12;
    for (const auto &c:channels) if (selected(c[0],caps)) {
        out[8+c[0]-1]++; put16(out+offset,c[1]);
        put32(out+offset+4,1u<<1); // IEEE80211_CHAN_NO_IR, cfg80211.h.
        offset+=8;
    }
    return size;
}
