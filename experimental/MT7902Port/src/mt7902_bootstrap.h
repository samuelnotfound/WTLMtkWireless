// SPDX-License-Identifier: BSD-3-Clause-Clear
// MT7902 bootstrap adaptations; reference: morrownr/mt76 c539593ef5649ac855e0995f2f3492c42f4f32eb.
#ifndef MT7902_BOOTSTRAP_H
#define MT7902_BOOTSTRAP_H
#include <stddef.h>
#include <stdint.h>

enum { MT7902_MAX_STAGE = 3, MT7902_MCU_TXQ = 15, MT7902_FWDL_TXQ = 16,
       MT7902_EVENT_COUNT = 512 };
struct Mt7902Prefetch { uint32_t ring, base; };
static const Mt7902Prefetch mt7902_rx_prefetch[] = {{0,0}, {1,0x40}, {2,0x80}, {3,0xc0}};
static const Mt7902Prefetch mt7902_tx_prefetch[] = {
    {0,0x100}, {1,0x140}, {2,0x180}, {3,0x1c0}, {4,0x200}, {5,0x240},
    {6,0x280}, {15,0x2c0}, {16,0x300}
};
struct Mt7902FwRegion { uint32_t offset, address, length, flags; };
static inline uint32_t mt7902_fw_be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static inline uint32_t mt7902_fw_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
// Preflight every region before sending any firmware command. Neither parser
// dereferences outside the supplied image, including for malformed region counts.
static inline bool mt7902_patch_region(const uint8_t *fw, size_t size, uint32_t index,
                                        Mt7902FwRegion *out) {
    if (!fw || !out || size<96) return false;
    uint32_t count=mt7902_fw_be32(fw+44);
    if (!count || count>(size-96)/64 || index>=count) return false;
    const uint8_t *sec=fw+96+(size_t)index*64;
    if ((mt7902_fw_be32(sec)&0xffff)!=2) return false;
    out->offset=mt7902_fw_be32(sec+4); out->address=mt7902_fw_be32(sec+12);
    out->length=mt7902_fw_be32(sec+16); out->flags=mt7902_fw_be32(sec+20);
    if (out->offset<96+(size_t)count*64 || out->offset>size ||
        !out->length || out->length>size-out->offset) return false;
    if (out->flags!=0xffffffffu && (out->flags>>24)>2) return false;
    return true;
}
static inline bool mt7902_patch_valid(const uint8_t *fw, size_t size) {
    if (!fw || size<96) return false;
    uint32_t count=mt7902_fw_be32(fw+44);
    if (!count || count>(size-96)/64) return false;
    Mt7902FwRegion region={};
    for (uint32_t i=0;i<count;i++) if (!mt7902_patch_region(fw,size,i,&region)) return false;
    return true;
}
static inline bool mt7902_ram_region(const uint8_t *fw, size_t size, uint32_t index,
                                      Mt7902FwRegion *out) {
    if (!fw || !out || size<36) return false;
    const uint8_t *trailer=fw+size-36;
    uint32_t count=trailer[2];
    if (!count || count>(size-36)/40 || index>=count) return false;
    size_t dataSize=size-36-(size_t)count*40,offset=0;
    for (uint32_t i=0;i<=index;i++) {
        const uint8_t *r=trailer-(size_t)(count-i)*40;
        uint32_t length=mt7902_fw_le32(r+20);
        if (offset>dataSize || length>dataSize-offset || offset>0xffffffffu) return false;
        if (i==index) {
            out->offset=(uint32_t)offset; out->address=mt7902_fw_le32(r+16);
            out->length=length; out->flags=r[24];
        }
        offset+=length;
    }
    return true;
}
static inline bool mt7902_ram_valid(const uint8_t *fw, size_t size) {
    if (!fw || size<36) return false;
    uint32_t count=fw[size-36+2];
    if (!count || count>(size-36)/40) return false;
    Mt7902FwRegion region={};
    for (uint32_t i=0;i<count;i++) if (!mt7902_ram_region(fw,size,i,&region)) return false;
    return true;
}
#endif
