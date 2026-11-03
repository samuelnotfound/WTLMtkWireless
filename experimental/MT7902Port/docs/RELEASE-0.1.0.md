# 0.1.0: MT7902 bootstrap foundation

Imports the BSD-licensed MT7922 macOS driver foundation at
`20aaac5c8e1d97d737a73f1a684ded2ec1d0f940`, retargets the driver and companion app,
and adapts the MT7902 PCI/ASIC checks, firmware images, command/event queue layout
and DMA prefetch map from pinned mt76 source.

The enabled scope stops at firmware-ready and DMA shutdown. Scan/connect/native
paths are disabled; no MT7902 connection or performance result is claimed.
The new image parser validates every firmware region before transfer and is
exercised on the actual MT7902 blobs and malformed/truncated inputs in host tests.

The older PCI-only diagnostic remains a separate product. Do not enable both
kexts on the same card. Read the top-level README before hardware testing.
