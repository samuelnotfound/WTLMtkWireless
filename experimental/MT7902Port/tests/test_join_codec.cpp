// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../codec/mt7902_join_codec.h"
#include "../codec/mt7902_tx_codec.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
int main()
{
    const uint8_t rsn[]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
    assert(mt7902_join_rsn(rsn,sizeof(rsn)));
    assert(!mt7902_join_rsn(nullptr,sizeof(rsn)));
    for (size_t n=0;n<sizeof(rsn);n++) assert(!mt7902_join_rsn(rsn,n));
    uint8_t bad[sizeof(rsn)];
    for (unsigned offset: {2u,7u,13u,19u}) {
        memcpy(bad,rsn,sizeof(bad)); bad[offset]=0xff;
        assert(!mt7902_join_rsn(bad,sizeof(bad)));
    }
    memcpy(bad,rsn,sizeof(bad)); bad[20]=64; // PMF required.
    assert(!mt7902_join_rsn(bad,sizeof(bad)));
    memcpy(bad,rsn,sizeof(bad)); bad[8]=255; bad[9]=255;
    assert(!mt7902_join_rsn(bad,sizeof(bad)));
    uint8_t minimal[20]; memcpy(minimal,rsn,20); minimal[1]=18;
    assert(mt7902_join_rsn(minimal,sizeof(minimal)));
    uint8_t transition[26]={}; memcpy(transition,rsn,20);
    transition[1]=24; transition[14]=2; transition[20]=0; transition[21]=15;
    transition[22]=172; transition[23]=8; transition[24]=128; // PMF capable, PSK+SAE.
    assert(mt7902_join_rsn(transition,sizeof(transition)));
    uint8_t guarded[22];
    for (unsigned ch=0;ch<256;ch++) {
        const bool allowed=(ch>=1 && ch<=11) || ch==36 || ch==40 || ch==44 || ch==48 ||
            ch==149 || ch==153 || ch==157 || ch==161 || ch==165;
        assert(mt7902_join_channel((uint8_t)ch)==allowed);
        memset(guarded,0xa5,sizeof(guarded));
        size_t n=mt7902_encode_join_domain(guarded+1,20,(uint8_t)ch);
        assert(n==(allowed?20u:0u));
        assert(guarded[0]==0xa5 && guarded[21]==0xa5);
        if (n) {
            assert(guarded[13]==ch && guarded[5]==1 && guarded[6]==1 && guarded[7]==1);
            assert(guarded[ch<=14?9:10]==1);
            for (unsigned i=17;i<21;i++) assert(guarded[i]==0);
        }
    }
    assert(!mt7902_encode_join_domain(guarded,19,1));
    assert(!mt7902_encode_join_domain(nullptr,20,1));
    assert(mt7902_fixed_spe(1)==0 && mt7902_fixed_spe(2)==1 && mt7902_fixed_spe(3)==24);
    uint8_t frame[30]={0xb0},tx[32]; Mt7902TxOptions opts={};
    opts.sequence=-1; opts.spe_index=mt7902_fixed_spe(1);
    assert(mt7902_encode_txwi(tx,frame,sizeof(frame),opts));
    assert(!(tx[29]&0xf8)); // Fixed-rate auth uses WF0, not duplicate WF0+WF1.
    // Firmware disconnect fixture: mt76_connac_mcu_sta_cmd(enable=false).
    const uint8_t peer[]={2,0x11,0x22,0x33,0x44,0x55};
    const uint8_t removed[]={
        0,1,2,0,1,0,0,0,
        0,0,20,0,2,0,1,0,0,0,0,0,2,0x11,0x22,0x33,0x44,0x55,1,0,
        13,0,12,0,1,1,0,0,0,0,0,0
    };
    uint8_t removal[42]; memset(removal,0xa5,sizeof(removal));
    assert(mt7902_encode_sta_remove(removal+1,40,0,1,peer)==40);
    assert(!memcmp(removal+1,removed,sizeof(removed)));
    assert(removal[0]==0xa5 && removal[41]==0xa5);
    for (size_t n=0;n<40;n++) {
        memset(removal,0xa5,sizeof(removal));
        assert(!mt7902_encode_sta_remove(removal+1,n,0,1,peer));
        for (uint8_t v:removal) assert(v==0xa5);
    }
    assert(!mt7902_encode_sta_remove(nullptr,40,0,1,peer));
    assert(!mt7902_encode_sta_remove(removal,40,0,1,nullptr));
    // CE CHIP_CONFIG fixture from mt76_connac_mcu_set_deep_sleep(false).
    uint8_t awake[330]; memset(awake,0xa5,sizeof(awake));
    assert(mt7902_encode_keep_awake(awake+1,328)==328);
    assert(awake[0]==0xa5 && awake[329]==0xa5);
    const char command[]="KeepFullPwr 1";
    assert(!memcmp(awake+9,command,sizeof(command)));
    for (unsigned i=1;i<9;i++) assert(awake[i]==0);
    for (unsigned i=9+sizeof(command);i<329;i++) assert(awake[i]==0);
    for (size_t n=0;n<328;n++) {
        memset(awake,0xa5,sizeof(awake));
        assert(!mt7902_encode_keep_awake(awake+1,n));
        for (uint8_t v:awake) assert(v==0xa5);
    }
    assert(!mt7902_encode_keep_awake(nullptr,328));
    // Linux UNI DEV_INFO_ACTIVE fixture; a local unicast MAC is allowed.
    const uint8_t ownMac[]={2,0x11,0x22,0x33,0x44,0x55};
    const uint8_t deviceExpected[]={0,0,0,0,0,0,12,0,1,0,2,0x11,0x22,0x33,0x44,0x55};
    uint8_t device[18]; memset(device,0xa5,sizeof(device));
    assert(mt7902_encode_device_info(device+1,16,ownMac)==16);
    assert(!memcmp(device+1,deviceExpected,sizeof(deviceExpected)));
    assert(device[0]==0xa5 && device[17]==0xa5);
    for (size_t n=0;n<16;n++) {
        memset(device,0xa5,sizeof(device));
        assert(!mt7902_encode_device_info(device+1,n,ownMac));
        for (uint8_t v:device) assert(v==0xa5);
    }
    const uint8_t zeroMac[6]={},groupMac[]={1,0,0,0,0,1};
    const uint8_t broadcastMac[]={255,255,255,255,255,255};
    assert(!mt7902_encode_device_info(nullptr,16,ownMac));
    assert(!mt7902_encode_device_info(device,16,nullptr));
    assert(!mt7902_encode_device_info(device,16,zeroMac));
    assert(!mt7902_encode_device_info(device,16,groupMac));
    assert(!mt7902_encode_device_info(device,16,broadcastMac));
    puts("join policy/RSN/domain/SPE tests passed");
    uint8_t ht[30]; memset(ht,0xa5,sizeof(ht));
    const uint8_t htExpected[28]={45,26,12,0,20,255,0,0,0,0,0,0,0,0,0,0,0,1};
    assert(mt7902_encode_ht20(ht+1,28,255)==28);
    assert(!memcmp(ht+1,htExpected,28) && ht[0]==0xa5 && ht[29]==0xa5);
    assert(mt7902_ht20_rate(255)==65 && mt7902_ht20_rate(1)==6 && mt7902_ht20_rate(0)==0);
    for (unsigned mask=1;mask<256;mask++) {
        assert(mt7902_encode_ht20(ht+1,28,(uint8_t)mask)==28);
        assert(ht[6]==mask && ht[7]==0 && ht[18]==1);
        assert((ht[3]&0x63)==0 && ht[4]==0); // no width, SGI, LDPC or STBC.
    }
    for (size_t n=0;n<28;n++) {
        memset(ht,0xa5,sizeof(ht));
        assert(!mt7902_encode_ht20(ht+1,n,255));
        for (uint8_t byte:ht) assert(byte==0xa5);
    }
    assert(!mt7902_encode_ht20(nullptr,28,255));
    assert(!mt7902_encode_ht20(ht,28,0));
    uint8_t reply[44]={}; reply[3]=0x38; reply[28]=5;
    reply[36]=0x90; reply[37]=0x40; reply[38]=0x0e; reply[39]=0x82;
    reply[40]=0x78; reply[41]=0x56; reply[42]=0x34; reply[43]=0x12;
    uint32_t registerValue=0;
    assert(mt7902_decode_register_reply(reply,44,0x820e4090,&registerValue));
    assert(registerValue==0x12345678);
    for (size_t n=0;n<44;n++) {
        registerValue=0xa5a5a5a5;
        assert(!mt7902_decode_register_reply(reply,n,0x820e4090,&registerValue));
        assert(registerValue==0xa5a5a5a5);
    }
    assert(!mt7902_decode_register_reply(nullptr,44,0x820e4090,&registerValue));
    assert(!mt7902_decode_register_reply(reply,44,0x820e4094,&registerValue));
    assert(!mt7902_decode_register_reply(reply,44,0x820e4090,nullptr));
    reply[28]=1; assert(!mt7902_decode_register_reply(reply,44,0x820e4090,&registerValue));
    reply[28]=5; reply[30]=4;
    assert(!mt7902_decode_register_reply(reply,44,0x820e4090,&registerValue));
    reply[30]=0; reply[3]=0;
    assert(!mt7902_decode_register_reply(reply,44,0x820e4090,&registerValue));
    // Fixed upstream CHANNEL_SWITCH layout; 20 MHz, normal reason, band0.
    for (uint8_t channel: {uint8_t(1),uint8_t(44)}) {
        uint8_t actual[78],expected[76]={}; memset(actual,0xa5,sizeof(actual));
        expected[0]=expected[1]=channel; expected[3]=expected[4]=1;
        expected[10]=channel>14?1:0;
        assert(mt7902_encode_channel_switch(actual+1,76,channel)==76);
        assert(!memcmp(actual+1,expected,76));
        assert(actual[0]==0xa5 && actual[77]==0xa5);
        for (size_t n=0;n<76;n++) {
            memset(actual,0xa5,sizeof(actual));
            assert(!mt7902_encode_channel_switch(actual+1,n,channel));
            for (uint8_t v:actual) assert(v==0xa5);
        }
    }
    uint8_t channelRequest[76];
    assert(!mt7902_encode_channel_switch(nullptr,76,1));
    for (uint8_t channel: {uint8_t(0),uint8_t(14),uint8_t(52),uint8_t(255)})
        assert(!mt7902_encode_channel_switch(channelRequest,76,channel));
}
