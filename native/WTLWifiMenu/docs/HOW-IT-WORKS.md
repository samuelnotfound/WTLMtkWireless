# How MtkWlanNative works

There are two kexts. **MtkWlan** owns the hardware: PCIe, firmware, DMA rings, scanning, association, WPA2, block ack. It exports a
C ops table (`MtkCoreApi.h`, version 4) through `callPlatformFunction`: scan, get_bss, join, disconnect, get_status,
set_mode, set_mac, tx_bytes, plus callbacks for RX frames, link state, scan done and join result. **MtkWlanNative** is
the front end macOS talks to.

`MtkWlanNative` (an `IO80211Controller`) matches the core, creates a CoreCapture logger and fault reporter (the Family
asks for both and panics without them), runs the Family's base start, then creates `MtkWlanNativeIf` (an
`IO80211InfraProtocol`: `IO80211InfraInterface` plus the 200 pure request handlers the Family routes ioctls to) and
starts it with a RegistrationInfo block: one packet pool, four Tx submission queues, Tx and Rx completion queues. The
interface binds the core (callbacks version 4) and tells it that the native front end owns the data path and the joins.

## Requests

Userland ioctls reach `IO80211Controller::apple80211Request`; the Family's dispatcher first asks the
interface `isCommandProhibited(code)` and only then calls the handler slot. Everything we do not implement answers
ENOTSUP, which userland treats as "not supported" and carries on; the allow-list in `MtkWlanNative.cpp` names what we
answer. Scan: `setWCL_SCAN_REQ` runs the core scan and the tick posts one 0xc9 event per cached BSS (a beacon message:
0x44-byte header, then the IEs), then 0xed. Join: `setWCL_ASSOCIATE` turns the candidate (SSID, cipher, key, BSSID)
into a core join; the result is announced as 0xd8 link status (the only event that takes the Family's net manager out
of LINK_DOWN), then 0x4e, 0xd3, 0xd4, 0xd5; the Family's link-up then reads the BSS info and extended BSS info from the
core's cache and sends its link-path configuration, which we accept. A link the core loses is reported as 0xd8 down.

## Data path

RX: the core hands Ethernet frames as mbufs under its gate; the interface copies them into a ring and
a 2 ms timer on the Family's work queue moves them into pool packets on the Rx completion queue. TX: the Tx submission
dequeue callback hands packet bytes to the core under the core's gate and completes the packets on the next tick. The
two gates never nest.

## Addresses

macOS gives a Wi-Fi interface a private address per network (through ioctl 0x170 before each join, or
directly on the BSD interface). The firmware only receives unicast for the address it was given, so the interface
follows every address change into the core (`set_mac`), which re-adds its station interface with it.

## ABI

Apple ships no headers for any of this. `tools/kcabi` dumps class sizes, vtables and the request dispatch
tables from the kernel collection of the running build; `genhdr.py` generates the headers and the stub file; the build
checks every override against Apple's slot numbers and every undefined symbol against the collection. `Info.plist`
lists the builds the headers were generated for and the kext refuses to load elsewhere.
