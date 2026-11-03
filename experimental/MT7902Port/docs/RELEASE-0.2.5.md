# MT7902 0.2.5: explicitly disable firmware deep sleep before joining

The 0.2.4 hardware capture shows four queued authentication requests, zero
TXFREE releases, zero matched TXS reports, and no authentication responses.
Seven received frames passed the RX decoder in the last exchange window. BSS
setup, station creation, channel grant and cleanup all returned success. This
does not establish that any request was radiated. A 5 GHz attempt also failed
according to the user, but its failure stage was not captured.

Linux mt7921_register_device leaves runtime PM and deep sleep disabled for
MT7902. After initializing the hardware, mt7921_init_work explicitly calls
mt76_connac_mcu_set_deep_sleep(false), which sends the text KeepFullPwr 1 in
the 328-byte CE CHIP_CONFIG request. The port omitted this firmware policy.
Driver ownership and the firmware's sleep policy are separate operations.

This version sends that same request before each stage-3 connection setup and
logs its transport result. firmware-awake-requested=Yes means the request was
submitted, not that a firmware acknowledgement or an RF transmission was
observed. The command follows Linux's no-response behavior. A fixture tests the
exact header, NUL-terminated command, padding, capacity and guard bytes.

No transmit-power table, antenna mask, channel policy, authentication timing or
TX packet format is changed. The command disables firmware sleep; its name
does not mean increasing the configured RF transmit-power ceiling.

Install both bundled files, retain mt7902_stage=3, reboot and try one visible
WPA2 network. Collect logs before rebooting. Include port-version, m3-log,
firmware-awake-requested, wifi-auth-diag, wifi-last-mgmt-tx and wifi-postmortem.
If testing both bands, collect separate logs after each attempt so the latest
attempt does not replace the first one's diagnostics.

This corrects a startup difference from Linux; the hypothesis that it causes
the observed missing completions remains unverified. Connection, key exchange,
DHCP and Ethernet traffic are still not validated on MT7902. Stage 2 remains
the default and fallback, and firmware is embedded in the kext.

References at the pinned mt76 revision c539593ef5649ac855e0995f2f3492c42f4f32eb:
- mt7921/init.c: mt7921_register_device and mt7921_init_work
- mt76_connac_mcu.c: mt76_connac_mcu_set_deep_sleep and chip_config
- mt76_connac_mcu.h: struct mt76_connac_config
