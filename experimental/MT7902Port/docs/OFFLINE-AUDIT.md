# Offline connection audit after 0.2.9

Purpose: reduce hardware reboot cycles by auditing related protocol boundaries
together. A successful host test or build cannot establish RF transmission.
No additional hardware test is requested by this audit.

Compared sources: pinned morrownr/mt76 c539593ef5649ac855e0995f2f3492c42f4f32eb
and hbyonly1/itlmt7922-wifi 20aaac5c8e1d97d737a73f1a684ded2ec1d0f940.

## Confirmed correction

BSS QBSS advertised qos=1 unconditionally, while the legacy checkpoint's
association request omits WMM and its AP STA BASIC and WTBL GENERIC flags
advertise qos=0. Linux mt76_connac_mcu_uni_add_bss takes the BSS QoS flag from
the negotiated configuration. Set QBSS from fastActive, matching the station
and association paths. This corrects inconsistent configuration; it is not
evidence that QoS caused the authentication timeout.

## Boundaries compared without finding a new mismatch

- MCU TX header: 64-byte legacy and 48-byte unified envelopes, TX bytes,
  command queue, long format, command header format, payload length, sequence,
  set/query, extension ID, extension ACK and destination fields match the
  pinned mt76_connac2_mcu_fill_message layouts for commands currently used.
- MAC enable and RTS requests: four-byte enable/band request and twelve-byte
  protection request match mt76_connac_mcu_set_mac_enable/set_rts_thresh.
- Legacy MCU responses: Linux itself generally matches sequence and does not
  decode extended-command firmware status. Our response rc=0 therefore must
  not be described as proven acceptance of the requested hardware setting.
- Register mapping: fixed MAC map entries, L1 mask, base/offset split and
  posted-write flush match the pinned mt7921 mapping helpers. Restoring the
  mapping is necessary because this port uses temporary remaps. Source agreement
  does not validate the MT7902's observed fixed-window behavior.
- Connection order: reference uses RX-path setup, BSS/STA, ROC, auth, assoc,
  then BSS/STA association, WPA2 keys and Ethernet. Its working MAC timing is
  firmware-managed. Our 0.2.8 timing experiment added a blocking readback check;
  0.2.9 removes it while retaining an explicit firmware home-channel command.
- Data TX ownership: payload buffers are held until TXFREE; DMA descriptor
  consumption alone does not release them. The captures still show consumption
  without completion reports, so this boundary remains hardware-unverified.

## What the existing captures establish

Firmware upload, capability queries, scanning, device registration, BSS/STA
setup and ROC replies work. Earlier builds usually timed out waiting for auth;
one 0.2.6 event reached association, whose details were overwritten. 0.2.7's
retained attempts showed no raw TXS/TXFREE reports during the auth exchange.
0.2.8 did not attempt auth because the timing readback check stopped setup.
No capture establishes successful WPA2 or Ethernet traffic.

## Remaining evidence needed before selecting another hardware correction

- Fixed-window versus L1 register values from the existing 0.2.9 diagnostic
  can separate differing access views from equal readbacks. Equal zeros alone
  do not prove either view is correct, nor justify writing through another view.
- DMA completion proves descriptor fetch, not payload visibility or RF delivery.
  CPU readback is not independent evidence of device-visible contents. Direct
  physical DMA currently has no IOMMU mapping support.
- A firmware-mediated register read would provide a third view, but its event
  address/value/length checks should be host-tested before use. Do not infer
  addresses or overwrite queue configuration based solely on zero counters.
- Preserve failed attempts and separate setup/transport failures from AP
  rejection and timeouts. Do not diagnose a password before the key handshake.

Only batch corrections supported by a source/layout comparison or captured
failure. Do not force extra antenna chains, raise transmit power, add broad
register writes, or claim a compiled build is a working connection.
