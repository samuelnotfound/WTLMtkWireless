# MT7902 0.2.7: register the station OMAC with firmware

The 0.2.6 hardware capture contains five authentication timeouts and one
association failure. The association stage is reached only after an accepted
open-system authentication response. Its detailed diagnostics were overwritten
by later attempts, so it is unknown whether association timed out or received
a rejection. No attempt reached the WPA2 key handshake or an Ethernet link.

In the final channel-44 authentication exchange, all four DMA descriptors were
consumed, but no raw TXS or TXFREE packets were observed. The ARB transmit/receive
disable bits were clear. The raw MIB counters and TX0 prefetch readback were
zero. Those observations do not identify a confirmed hardware fault, and this
version does not rewrite queue registers based on those readbacks.

Comparison with the pinned Linux mt76 source found a missing setup command:
mt76_connac_mcu_uni_add_dev registers the station's own MAC through
UNI DEV_INFO_UPDATE / DEV_INFO_ACTIVE before configuring the BSS. Reading the
firmware's capability MAC does not perform that registration. This port
previously only sent BSS/STA setup with the AP address.

This version sends the 16-byte OMAC0/band0 activation request using the reported
station MAC before the first BSS setup. It requires a successful firmware reply,
and repeats activation after firmware reload. A rejection stops the join with
device-setup-fail. device-info-ready and device-info-result expose the outcome.
Successful activation confirms command acceptance, not over-the-air delivery.
The change corrects an upstream initialization omission; it is not a confirmed
fix for the observed timeouts.

wifi-attempt-history retains the last eight attempt snapshots, including the
stage, result, channel, management diagnostics and setup log, across retries
and firmware recovery within the same boot. The list is bounded and contains
no passwords, keys or frame payloads. Association timeouts now have a separate
assoc-timeout-fail stage and an accurate app message, rather than claiming the
access point refused association when no response arrived.

Validation includes an upstream-layout device-info fixture, guarded output
bounds, invalid/multicast/zero MAC rejection, sanitized host protocol tests,
x86_64 kext/app compilation and package/embedded-firmware checks in Actions.
Hardware connection, WPA2 handshake, DHCP and Ethernet traffic remain unverified.

Install both bundles, retain mt7902_stage=3, reboot, try one network and collect
logs before rebooting. No additional boot argument or separate firmware file
is needed. Stage 2 remains the default and scanning fallback.
