// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors; MediaTek Inc. protocol references.
// Connac2 scan/capability/receive port; 36-byte events and packed CE scan.
#include "itlmt7902.hpp"
#ifdef MTK_WITH_MT7902
#include "Mtk7902Backend.hpp"
#endif
#include "mt7902_regs.h"
#include "../codec/mt7902_rx_codec.h"
#include <string.h>
#include <stdarg.h>
#include <libkern/libkern.h>
static inline uint32_t le32(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static inline uint16_t le16(const uint8_t *p) { return (uint16_t)((uint32_t)p[0]|(uint32_t)p[1]<<8); }
static inline void put16(uint8_t *p,uint16_t v) { memcpy(p,&v,2); }
static inline void put32(uint8_t *p,uint32_t v) { memcpy(p,&v,4); }
static inline void barrier() { __sync_synchronize(); }
static const uint32_t kCe=1u<<18,kUni=1u<<17,kQuery=1u<<16;

bool itlmt7902::ringPeek(itl_ring &r,const uint8_t **data,uint32_t *length,int *index)
{
    if (!r.n || !r.desc.va || !r.buf.va || r.tail==r.head) return false;
    const uint32_t ctrl=le32(r.desc.va+r.tail*16+4);
    if (!(ctrl & MT_DMA_CTL_DMA_DONE)) return false;
    barrier();
    const unsigned ring=&r==&rxEvt?0:&r==&rxWa?1:&r==&rxData?2:3;
    int bi=r.slotBuf[r.tail];
    if (bi<0 || bi>=r.n) {
        // An invalid mapping cannot safely be consumed/refilled. This counts
        // failed observations, since a later poll may encounter it again.
        if (ring<3) rxDmaBadIndex[ring]++;
        return false;
    }
    *index=bi; *data=r.buf.va+(size_t)bi*MT_RX_BUF_SIZE;
    *length=(ctrl>>16)&0x3fff;
    if (ring<3) {
        rxDmaDesc[ring]++;
        // These are independent raw-descriptor categories: a descriptor can
        // be both oversized and non-final. No fragment reassembly is added.
        if (!*length) rxDmaZero[ring]++;
        if (*length>MT_RX_BUF_SIZE) rxDmaOversize[ring]++;
        if (!(ctrl & MT_DMA_CTL_LAST_SEC0)) rxDmaFragment[ring]++;
        if (*length>=4 && *length<=MT_RX_BUF_SIZE && (ctrl & MT_DMA_CTL_LAST_SEC0)) {
            const uint32_t d0=le32(*data),type=d0>>27,flag=(d0>>16)&15;
            if (type==2 || type==8 || (type==7 && flag==1)) rxDmaFrames[ring]++;
        }
    }
    if (*length>MT_RX_BUF_SIZE || !(ctrl & MT_DMA_CTL_LAST_SEC0)) *length=0;
    return true;
}
void itlmt7902::ringRelease(itl_ring &r,int bi)
{
    if (!r.n || bi<0 || bi>=r.n) return;
    uint8_t *d=r.desc.va+r.head*16;
    put32(d,(uint32_t)(r.buf.pa+(uint64_t)bi*MT_RX_BUF_SIZE));
    put32(d+8,0); put32(d+12,0); put32(d+4,MT_DMA_CTL_SD_LEN0(MT_RX_BUF_SIZE));
    r.slotBuf[r.head]=(uint16_t)bi; r.head=(r.head+1)%r.n; r.tail=(r.tail+1)%r.n;
    barrier(); wr(r.regs+Q_CPU_IDX,(uint32_t)r.head);
}
void itlmt7902::m2State(const char *tag)
{
    m2log("%s LP=%08x MISC=%08x GLO=%08x RX0=%u RX4=%u RXdata=%u",tag,
          rdA(MT_CONN_ON_LPCTL),rdA(MT_CONN_ON_MISC),rdA(MT_WFDMA0_GLO_CFG),
          rxEvt.n?rd(rxEvt.regs+Q_DMA_IDX):0,rxWa.n?rd(rxWa.regs+Q_DMA_IDX):0,
          rxData.n?rd(rxData.regs+Q_DMA_IDX):0);
}
int itlmt7902::uniSend(uint8_t cid,const void *payload,size_t plen,bool query,
                      uint8_t *out,size_t max,uint32_t *length,uint32_t timeout)
{
    int status=0;
    return mcuSend(kUni | cid | (query?kQuery:0),payload,plen,true,&status,out,max,length,timeout);
}
void itlmt7902::handleRxPacket(const uint8_t *p,uint32_t length)
{
    if (length<4) return;
    const uint32_t d0=le32(p),type=d0>>27,flag=(d0>>16)&15;
    rxPacketTypes[type]++; // Count before parsing/filtering completion reports.
    if (m3wantFc && (type==0 || type==6)) {
        uint32_t words[6]={};
        const size_t bytes=length<sizeof(words)?length:sizeof(words);
        memcpy(words,p,bytes);
        char raw[160];
        snprintf(raw,sizeof(raw),"bytes=%u header=%08x/%08x body=%08x/%08x/%08x/%08x",
            length,words[0],words[1],words[2],words[3],words[4],words[5]);
        setProperty(type==0?"wifi-txs-raw":"wifi-txfree-raw",raw);
    }
    if (type==0) { m3TxStatus(p,length); return; }
    if (type==6) { m3TxFree(p,length); return; }
    if (type==2 || type==8 || (type==7 && flag==1)) {
        rxPkts++;
        rxParseFrame(p,length);
        return;
    }
    if (type!=7 || length<36) return;
    evCount++;
    const bool uniUnsol=(p[30]&4)!=0;
    if ((!uniUnsol && p[28]==0xf0) || (uniUnsol && p[28]==0x0a)) {
        firmwareFault=true; recoverWanted=true; m3lost=true;
        strlcpy(m3lostWhy,"firmware coredump",sizeof(m3lostWhy));
        m2log("firmware coredump eid=%02x namespace=%u",p[28],uniUnsol?1:0);
        return;
    }
    if (!uniUnsol && p[28]==0x0d && length>=52 && p[36]==scanSeq) {
        scanDone=true;
        m2log("scan_done seq=%u channels=%u state=%u",p[36],p[40],p[41]);
    }
    m3Event(p,length);
}
void itlmt7902::rxDataPoll()
{
    // Loop-entry gap while associated. MCU waits also drain the RX rings
    // directly, so this is not a claim that DMA went unserviced throughout.
    const uint32_t now=nowMs();
    if (m3linkUp) {
        if (rxDmaLastPollMs) {
            const uint32_t gap=now-rxDmaLastPollMs;
            if (gap>rxDmaMaxGapMs) rxDmaMaxGapMs=gap;
        }
        rxDmaLastPollMs=now;
    } else rxDmaLastPollMs=0;
    itl_ring *rings[]={&rxEvt,&rxData}; // MT7902 shares event/TX-done ring 0; no MCU-WA ring.
    const unsigned diagnosticSlot[]={0,2};
    for (unsigned q=0;q<2;q++) {
        const uint8_t *p; uint32_t length; int bi;
        unsigned consumed=0;
        for (;consumed<128 && !shouldStop() && ringPeek(*rings[q],&p,&length,&bi);consumed++) {
            handleRxPacket(p,length); ringRelease(*rings[q],bi);
            if (firmwareFault) break;
        }
        // Inspect the next descriptor without another ringPeek(), which would
        // count it twice before consumption on the following pass.
        const itl_ring &r=*rings[q];
        if (consumed==128 && r.n && r.desc.va && r.tail!=r.head &&
            (le32(r.desc.va+r.tail*16+4)&MT_DMA_CTL_DMA_DONE)) rxDmaBudget[diagnosticSlot[q]]++;
    }
    ethFlush();
}
void itlmt7902::rxFrameDump(const uint8_t *p,uint32_t length)
{
    if (rxDump>=3 || length<24) return;
    rxDump++;
    m2log("RX len=%u d0=%08x d1=%08x d2=%08x",length,le32(p),le32(p+4),le32(p+8));
}
void itlmt7902::rxParseFrame(const uint8_t *p,uint32_t length)
{
    if (m3wantFc && m3RxMgmt(p,length)) return;
    if (m3dataOn && m3RxCtl(p,length)) return;
    if (m3dataOn && m3RxData(p,length)) return;
    Mt7902RxView view={};
    if (mt7902_parse_rx_normal(p,length,&view)!=MT7902_RX_OK) { rxOther++; return; }
    const uint8_t *f=p+view.frame_offset;
    const uint32_t n=view.frame_length;
    if ((m3linkUp || m3wantEapol) && n>=26 &&
        ((f[0]&0xfc)==0xc0 || (f[0]&0xfc)==0xa0) &&
        !memcmp(f+10,tgtBssid,6) && !memcmp(f+16,tgtBssid,6) &&
        (!memcmp(f+4,mac,6) || (f[4]&1))) {
        uint16_t reason=le16(f+24);
        if (m3wantEapol) { m3hsApLeave=true; m3hsApReason=reason; }
        else { m3lost=true; snprintf(m3lostWhy,sizeof(m3lostWhy),"AP disconnect reason %u",reason); }
        return;
    }
    Mt7902Beacon beacon={};
    if (!mt7902_parse_beacon(f,n,&beacon)) { rxOther++; return; }
    char ssid[33]={}; memcpy(ssid,beacon.ssid,beacon.ssid_length);
    const uint8_t channel=beacon.channel?beacon.channel:view.channel;
    rxBeacons++;
    if (channel<sizeof(chHist) && chHist[channel]!=255) chHist[channel]++;
    m3Capture(f,n,ssid,channel);
    apSeen(p,view.packet_length,view.frame_offset,ssid,channel);
}
int itlmt7902::m2Scan(const char *ssid)
{
    if (shouldStop() || firmwareFault) return -5;
    if (ssid && ssid[0]) return -95; // Directed/active scans await regulatory and TX validation.
    uint8_t domain[MT7902_SCAN_DOMAIN_MAX]={};
    size_t domainBytes=mt7902_encode_scan_domain(domain,sizeof(domain),nicCaps);
    if (!domainBytes) return -22;
    int domainRc=mcuSend(kCe|0x0f,domain,domainBytes,false,nullptr);
    if (domainRc) return domainRc;
    uint8_t scan[MT7902_PASSIVE_SCAN_BYTES]={};
    scanSeq=(uint8_t)((scanSeq+1)&0x7f); if (!scanSeq) scanSeq=1;
    if (!mt7902_encode_passive_scan(scan,sizeof(scan),scanSeq,nicCaps)) return -22;
    IOLockLock(cmdLock); lastScanReqMs=nowMs(); IOLockUnlock(cmdLock);
    wifiState("scanning",m3On?"Listening for nearby networks":"Listening for beacons; connections are disabled");
    scanDone=false; bzero(chHist,sizeof(chHist));
    int rc=mcuSend(kCe|3,scan,sizeof(scan),false,nullptr);
    m2log("CE passive scan submit rc=%d seq=%u channels=%u",rc,scanSeq,scan[159]);
    if (rc) return rc;
    const uint32_t start=nowMs();
    uint32_t publish=start;
    while (!shouldStop() && !firmwareFault && !scanDone && (uint32_t)(nowMs()-start)<20000) {
        rxDataPoll();
        if ((uint32_t)(nowMs()-publish)>=1000) { m2Publish(); publish=nowMs(); }
        IOSleep(1);
    }
    scanMs=nowMs()-start;
    if (!scanDone && !shouldStop() && !firmwareFault) {
        uint8_t cancel[4]={scanSeq,0,0,0};
        mcuSend(kCe|0x1b,cancel,4,false,nullptr);
    }
    if (scanDone) for (unsigned i=0;i<300 && !shouldStop() && !firmwareFault;i++) { rxDataPoll(); IOSleep(1); }
    m2Publish();
    scanGen++; wifiNets();
    m2log("CE scan done=%u ms=%u aps=%d rx=%u bcn=%u",scanDone?1:0,scanMs,nAps,rxPkts,rxBeacons);
    return shouldStop()?-125:firmwareFault?-5:scanDone?0:-110;
}
int itlmt7902::m2Run()
{
    uint8_t event[2048]={}; uint32_t length=0; int status=0;
    int rc=mcuSend(kCe|0x8a,nullptr,0,true,&status,event,sizeof(event),&length,5000);
    if (rc || length<40) { m2log("NIC capability rc=%d len=%u",rc,length); return rc?rc:-22; }
    if (!mt7902_decode_nic_caps(event+36,length-36,&nicCaps)) {
        m2log("NIC capability MAC/PHY parse failed len=%u",length);
        return -22;
    }
    memcpy(mac,nicCaps.mac,6);
#ifdef MTK_WITH_MT7902
    if (mtkBridge) mtkBridge->restoreMac();
#endif
    setProperty("nic-nss",nicCaps.nss,32); setProperty("nic-chain-mask",nicCaps.chain_mask,32);
    setProperty("nic-band-mask",nicCaps.bands,32); setProperty("nic-he",nicCaps.he,32);
    m2log("NIC capabilities nss=%u chains=%x bands=%x ht=%u vht=%u he=%u bw=%u",
        nicCaps.nss,nicCaps.chain_mask,nicCaps.bands,nicCaps.ht,nicCaps.vht,nicCaps.he,nicCaps.max_bw);
    setProperty("IOMACAddress",mac,6); setProperty("itlmt_mac_source","firmware");
    m2log("MAC=%02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    publishService();
    rc=m3HwPrep(); if (rc) { m2log("RX MAC setup failed rc=%d",rc); return rc; }
    uint8_t dev[16]={}; put16(dev+4,0); put16(dev+6,12); dev[8]=1; memcpy(dev+10,mac,6);
    rc=uniSend(1,dev,sizeof(dev),false,event,sizeof(event),&length,3000);
    if (rc) { m2log("device-info update failed rc=%d",rc); return rc; }
    uint8_t bss[36]={}; uint8_t *b=bss+4;
    put16(b+2,32); b[4]=1; put32(b+8,0x10001); b[12]=1; put16(b+20,19); put16(b+26,19);
    rc=uniSend(2,bss,sizeof(bss),false,event,sizeof(event),&length,3000);
    if (rc) { m2log("BSS-info update failed rc=%d",rc); return rc; }
    return m2Scan(nullptr);
}
// Single hardware worker remains alive for app-triggered passive rescans.
// No MCU command runs on the user-client thread, and no join/disconnect is served.
void itlmt7902::m2ServeScans()
{
    setProperty("scan-supported",kOSBooleanTrue);
    __atomic_store_n(&scanControlReady,true,__ATOMIC_RELEASE);
    wifiState("scan-ready","Scan-only test: connecting is not enabled");
    while (!shouldStop() && !firmwareFault) {
        if (cmdTake(CMD_SCAN,nullptr)&CMD_SCAN) {
            int rc=m2Scan(nullptr);
            setProperty("scan-result",(uint32_t)rc,32);
            if (rc) { wifiState("scan-failed","Passive rescan failed; collect m2-log"); break; }
            wifiState("scan-ready","Scan-only test: connecting is not enabled");
        }
        rxDataPoll();
        IOSleep(20);
    }
    __atomic_store_n(&scanControlReady,false,__ATOMIC_RELEASE);
    setProperty("scan-supported",kOSBooleanFalse);
    if (firmwareFault) wifiState("scan-failed","Firmware fault during scan; collect initialization logs");
}
void itlmt7902::m2log(const char *fmt, ...)
{
    char tmp[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    IOLog("itlmt7902: M2 %s\n", tmp);
    size_t n = strlen(tmp);
    if (m2len + n + 2 < sizeof(m2buf)) {
        memcpy(m2buf + m2len, tmp, n);
        m2len += n;
        m2buf[m2len++] = ';';
        m2buf[m2len] = 0;
    }
    setProperty("m2-log", m2buf);
}

void itlmt7902::m2Publish()
{
    size_t n = 0;
    m2res[0] = 0;
    n += (size_t)snprintf(m2res + n, sizeof(m2res) - n, "rx=%u bcn=%u oth=%u ap=%d sd=%d@%ums h:", rxPkts, rxBeacons, rxOther, nAps,
                  scanDone ? 1 : 0, scanMs);
    for (unsigned c = 0; c < sizeof(chHist) && n + 12 < sizeof(m2res); c++)
        if (chHist[c]) n += (size_t)snprintf(m2res + n, sizeof(m2res) - n, "%u=%u,", c, chHist[c]);
    n += (size_t)snprintf(m2res + n, sizeof(m2res) - n, ":");
    for (int i = 0; i < nAps && n + 40 < sizeof(m2res); i++)
        n += (size_t)snprintf(m2res + n, sizeof(m2res) - n, " %s/%u/%02x%02x", aps[i].ssid, aps[i].ch, aps[i].bssid[4],
                      aps[i].bssid[5]);
    setProperty("m2-res", m2res);
}
