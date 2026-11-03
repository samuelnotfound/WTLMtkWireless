// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "mt7902_join_codec.h"
#include <string.h>
namespace {
uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
bool suite(const uint8_t *p, uint8_t type) { return !p[0] && p[1]==15 && p[2]==172 && p[3]==type; }
}
bool mt7902_join_channel(uint8_t c)
{
    return (c>=1 && c<=11) || (c>=36 && c<=48 && c%4==0) ||
           c==149 || c==153 || c==157 || c==161 || c==165;
}
bool mt7902_join_rsn(const uint8_t *ie, size_t n)
{
    if (!ie || n<20 || ie[0]!=48 || n!=(size_t)ie[1]+2 || le16(ie+2)!=1 || !suite(ie+4,4)) return false;
    size_t pos=8;
    auto list=[&](uint8_t kind)->bool {
        if (n-pos<2) return false;
        unsigned count=le16(ie+pos); pos+=2;
        if (!count || count>(n-pos)/4) return false;
        bool found=false;
        for (unsigned i=0;i<count;i++,pos+=4) if (suite(ie+pos,kind)) found=true;
        return found;
    };
    if (!list(4) || !list(2)) return false; // CCMP pairwise and ordinary PSK AKM.
    if (pos==n) return true;
    if (n-pos<2 || (le16(ie+pos)&(1u<<6))) return false; // PMF required is unsupported.
    pos+=2;
    if (pos==n) return true;
    if (n-pos<2) return false;
    unsigned pmkids=le16(ie+pos); pos+=2;
    if (pmkids>(n-pos)/16) return false;
    pos+=16*pmkids;
    return pos==n || (n-pos==4 && suite(ie+pos,6));
}
size_t mt7902_encode_join_domain(uint8_t *out,size_t capacity,uint8_t channel)
{
    if (!out || capacity<20 || !mt7902_join_channel(channel)) return 0;
    memset(out,0,20);
    out[0]='0'; out[1]='0'; out[4]=out[5]=out[6]=1; // BW20.
    out[channel<=14?8:9]=1;
    out[12]=channel; // Only the selected, observed AP channel is enabled.
    return 20;
}
uint8_t mt7902_fixed_spe(uint8_t mask)
{
    // mt76_connac_spe_idx: WF0-only is index 0; duplicate WF0/WF1 is 24.
    static const uint8_t table[]={0,0,1,24,3,2,4,24,9,8,6,10,16,12,18,24};
    return mask<sizeof(table)?table[mask]:0;
}
size_t mt7902_encode_ht20(uint8_t *out,size_t capacity,uint8_t mask)
{
    if (!out || capacity<28 || !mask) return 0;
    memset(out,0,28);
    out[0]=45; out[1]=26; out[2]=0x0c; // SMPS disabled; HT20, long GI.
    out[4]=5u<<2; // Max A-MPDU 8191, minimum MPDU spacing 4 us.
    out[5]=mask; out[17]=1; // RX MCS0..7; TX MCS set defined, same as RX.
    return 28;
}
uint32_t mt7902_ht20_rate(uint8_t mask)
{
    static const uint8_t rate[]={6,13,19,26,39,52,58,65};
    for (unsigned i=8;i>0;i--) if (mask&(1u<<(i-1))) return rate[i-1];
    return 0;
}
size_t mt7902_encode_keep_awake(uint8_t *out,size_t capacity)
{
    if (!out || capacity<328) return 0;
    // struct mt76_connac_config: id/type/resp_type/data_size/reserved,
    // then data[320]. Linux leaves the header zero for this text command.
    static const char command[]="KeepFullPwr 1";
    memset(out,0,328);
    memcpy(out+8,command,sizeof(command));
    return 328;
}
bool mt7902_decode_register_reply(const uint8_t *event,size_t length,
                                  uint32_t address,uint32_t *value)
{
    if (!event || !value || length<44 || event[28]!=5 || (event[30]&4)) return false;
    auto get32=[](const uint8_t *p)->uint32_t {
        return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
    };
    if ((get32(event)>>27)!=7 || get32(event+36)!=address) return false;
    *value=get32(event+40);
    return true;
}
size_t mt7902_encode_channel_switch(uint8_t *out,size_t capacity,uint8_t channel)
{
    if (!out || capacity<76 || !mt7902_join_channel(channel)) return 0;
    // mt7921_mcu_set_chan_info(CHANNEL_SWITCH), NL80211_CHAN_WIDTH_20.
    memset(out,0,76);
    out[0]=out[1]=channel;
    out[3]=out[4]=1; // stream counts (SET_RX_PATH instead takes an RX mask).
    out[10]=channel>14?1:0;
    return 76;
}
size_t mt7902_encode_device_info(uint8_t *out,size_t capacity,const uint8_t mac[6])
{
    if (!out || !mac || capacity<16 || (mac[0]&1)) return 0;
    uint8_t nonzero=0;
    for (unsigned i=0;i<6;i++) nonzero|=mac[i];
    if (!nonzero) return 0;
    // mt76_connac_mcu_uni_add_dev: four-byte OMAC0/band0 header,
    // DEV_INFO_ACTIVE tag 0, length 12, active=1, link_idx=0, address.
    memset(out,0,16);
    out[6]=12; out[8]=1;
    memcpy(out+10,mac,6);
    return 16;
}
size_t mt7902_encode_sta_remove(uint8_t *out, size_t capacity, uint8_t bss,
                                uint8_t wcid, const uint8_t peer[6])
{
    if (!out || !peer || capacity<40) return 0;
    memset(out,0,40);
    out[0]=bss; out[1]=wcid; out[2]=2; out[4]=1; // append, two top-level TLVs.
    out[10]=20; // BASIC tag 0, length 20.
    out[12]=2; out[14]=1; // CONNECTION_INFRA_AP = 0x00010002.
    memcpy(out+20,peer,6);
    out[26]=1; // EXTRA_INFO_VER, not EXTRA_INFO_NEW.
    out[28]=13; out[30]=12; // WTBL tag 13 includes its eight-byte header.
    out[32]=wcid; out[33]=1; // RESET_AND_SET, zero nested TLVs when disabled.
    return 40;
}
