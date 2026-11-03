// SPDX-License-Identifier: BSD-3-Clause-Clear
// Exercise the same image preflight routines called by the MT7902 kernel loader.
#include "../src/mt7902_bootstrap.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

static std::vector<uint8_t> read(const char *path) {
    std::ifstream stream(path,std::ios::binary);
    assert(stream.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
}
int main(int argc,char **argv) {
    assert(argc==3);
    auto patch=read(argv[1]),ram=read(argv[2]);
    assert(patch.size()==119328 && ram.size()==716944);
    assert(mt7902_patch_valid(patch.data(),patch.size()));
    assert(mt7902_ram_valid(ram.data(),ram.size()));
    Mt7902FwRegion region={};
    assert(mt7902_patch_region(patch.data(),patch.size(),0,&region));
    assert(region.address==0x900000 && region.offset==160 && region.length==119168);
    assert(!mt7902_patch_region(patch.data(),patch.size(),1,&region));
    const uint32_t addresses[]={0x918400,0x2015c00,0x404400,0};
    const uint32_t lengths[]={414608,287696,10768,3600};
    uint32_t offset=0;
    for (unsigned i=0;i<4;i++) {
        assert(mt7902_ram_region(ram.data(),ram.size(),i,&region));
        assert(region.address==addresses[i] && region.length==lengths[i] && region.offset==offset);
        assert((region.flags&64)==(i==3?64u:0u));
        offset+=lengths[i];
    }
    assert(!mt7902_ram_region(ram.data(),ram.size(),4,&region));
    assert(!mt7902_patch_valid(nullptr,0) && !mt7902_ram_valid(nullptr,0));
    for (size_t n=0;n<160;n++) assert(!mt7902_patch_valid(patch.data(),n));
    for (size_t n=0;n<36;n++) assert(!mt7902_ram_valid(ram.data(),n));
    auto bad=patch;
    for (unsigned i=0;i<4;i++) bad[44+i]=255; // Overflowing section count.
    assert(!mt7902_patch_valid(bad.data(),bad.size()));
    bad=patch; bad[100]=255; // Payload offset outside the image.
    assert(!mt7902_patch_valid(bad.data(),bad.size()));
    bad=patch; bad[112]=255; // Payload size outside the image.
    assert(!mt7902_patch_valid(bad.data(),bad.size()));
    bad=ram; bad[bad.size()-36+2]=255;
    assert(!mt7902_ram_valid(bad.data(),bad.size()));
    bad=ram;
    size_t first=bad.size()-36-4*40;
    for (unsigned i=0;i<4;i++) bad[first+20+i]=255;
    assert(!mt7902_ram_valid(bad.data(),bad.size()));
    // Short arbitrary inputs must be bounded even when they imitate region counts.
    uint32_t state=7;
    for (size_t size=0;size<256;size++) {
        std::vector<uint8_t> input(size);
        for (auto &b:input) { state=state*1664525u+1013904223u; b=(uint8_t)(state>>24); }
        (void)mt7902_patch_valid(input.data(),input.size());
        (void)mt7902_ram_valid(input.data(),input.size());
    }
    assert(MT7902_MCU_TXQ==15 && MT7902_FWDL_TXQ==16 && MT7902_EVENT_COUNT==512);
    assert(sizeof(mt7902_rx_prefetch)/sizeof(mt7902_rx_prefetch[0])==4);
    assert(mt7902_tx_prefetch[7].ring==15 && mt7902_tx_prefetch[7].base==0x2c0);
    assert(mt7902_tx_prefetch[8].ring==16 && mt7902_tx_prefetch[8].base==0x300);
    std::cout << "MT7902 bootstrap image and malformed-input checks passed\n";
}
