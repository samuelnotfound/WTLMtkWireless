// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../codec/mt7902_scan_codec.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
static void put32(uint8_t *p,uint32_t value) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(8*i)); }
static std::vector<uint8_t> cap(uint8_t nss,uint8_t bands) {
    std::vector<uint8_t> p(4+8+6+8+12,0);
    p[0]=2; put32(p.data()+4,7); put32(p.data()+8,6);
    const uint8_t mac[]={2,0x11,0x22,0x33,0x44,0x55}; memcpy(p.data()+12,mac,6);
    put32(p.data()+18,8); put32(p.data()+22,12);
    p[26]=p[27]=p[37]=1; p[29]=2; p[30]=nss; p[36]=bands;
    return p;
}
int main() {
    auto p=cap(1,3); Mt7902NicCaps c={};
    assert(mt7902_decode_nic_caps(p.data(),p.size(),&c));
    assert(c.nss==1 && c.chain_mask==1 && c.bands==3 && c.mac[5]==0x55);
    for (size_t n=0;n<p.size();n++) assert(!mt7902_decode_nic_caps(p.data(),n,&c));
    for (auto nss:{0,5,255}) { auto bad=cap((uint8_t)nss,3); assert(!mt7902_decode_nic_caps(bad.data(),bad.size(),&c)); }
    auto bad=cap(1,0); assert(!mt7902_decode_nic_caps(bad.data(),bad.size(),&c));
    bad=p; bad[12]=1; assert(!mt7902_decode_nic_caps(bad.data(),bad.size(),&c));
    bad=p; memset(bad.data()+12,0,6); assert(!mt7902_decode_nic_caps(bad.data(),bad.size(),&c));
    bad=p; put32(bad.data()+8,0xffffffff); assert(!mt7902_decode_nic_caps(bad.data(),bad.size(),&c));
    bad=p; put32(bad.data()+18,7); assert(!mt7902_decode_nic_caps(bad.data(),bad.size(),&c));
    bad=p; bad.push_back(1); assert(!mt7902_decode_nic_caps(bad.data(),bad.size(),&c));
    bad=p; bad.resize(bad.size()+3,0); assert(mt7902_decode_nic_caps(bad.data(),bad.size(),&c));
    uint8_t scan[MT7902_PASSIVE_SCAN_BYTES+2],domain[MT7902_SCAN_DOMAIN_MAX+2];
    for (uint8_t bands=1;bands<=3;bands++) {
        p=cap(1,bands); assert(mt7902_decode_nic_caps(p.data(),p.size(),&c));
        memset(scan,0xa5,sizeof(scan)); memset(domain,0xa5,sizeof(domain));
        assert(mt7902_encode_passive_scan(scan+1,sizeof(scan)-2,3,c)==MT7902_PASSIVE_SCAN_BYTES);
        assert(scan[0]==0xa5 && scan[sizeof(scan)-1]==0xa5);
        auto s=scan+1;
        assert(s[0]==3 && s[2]==0 && s[3]==1 && s[4]==0 && s[5]==0 && s[7]==1);
        unsigned channels=bands==1?13:bands==2?9:22;
        assert(s[159]==channels && s[826]==0 && s[827]==0 && s[1185]==0);
        for (unsigned i=0;i<channels;i++) {
            uint8_t band=s[160+i*2],channel=s[161+i*2];
            assert((bands&(1u<<(band-1))) && (channel<=13 || channel==36 || channel==40 || channel==44 || channel==48 || channel>=149));
        }
        size_t bytes=mt7902_encode_scan_domain(domain+1,sizeof(domain)-2,c);
        assert(bytes==12+channels*8 && domain[0]==0xa5 && domain[bytes+1]==0xa5);
        auto d=domain+1;
        assert(d[4]==1 && d[5]==1 && d[6]==1 && (unsigned)d[8]+d[9]==channels && d[10]==0);
        for (unsigned i=0;i<channels;i++) assert(d[12+i*8+4]==2 && d[12+i*8]==s[161+i*2]);
        assert(!mt7902_encode_passive_scan(scan,10,3,c));
        assert(!mt7902_encode_scan_domain(domain,bytes-1,c));
    }
    assert(!mt7902_encode_passive_scan(scan,sizeof(scan),0,c));
    assert(!mt7902_encode_passive_scan(scan,sizeof(scan),128,c));
    std::cout << "MT7902 capability parsing and passive scan wire checks passed\n";
}
