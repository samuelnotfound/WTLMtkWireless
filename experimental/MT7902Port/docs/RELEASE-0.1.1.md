# 0.1.1: passive scan checkpoint

The user's 0.1.0 screenshot reports MT7902 ASIC 0x7902 revision 0x8a00,
successful patch/RAM upload, firmware-ready Yes, and successful DMA cleanup.
That establishes the firmware checkpoint for that machine, not Wi-Fi connectivity.

This build defaults to stage 2. It reads the firmware MAC and PHY TLVs, validates
the entire response, derives the receive chain mask from NSS, initializes the RX
data ring, and submits passive CE scans on a restricted 2.4/5 GHz channel list.
The channel domain carries NO_IR and 20 MHz restrictions; no probe requests or
directed scans are requested. The app displays discovered networks and can request
rescans on the hardware worker. The unavailable MCU-WA ring is not polled.

Connection stages remain disabled, including passwords, WPA2, Ethernet attachment
and native Wi-Fi. Stage 1 remains available with mt7902_stage=1. A scan failure
exits the worker and quiesces DMA. Successful stage 2 leaves the worker/DMA active
until stop or an error, unlike the firmware-only checkpoint.

New sanitizer tests cover truncated/oversized/duplicate capability TLVs, invalid
MAC/NSS values, band filtering, passive wire fields and output buffer boundaries.
They do not establish successful scanning on real MT7902 hardware.
