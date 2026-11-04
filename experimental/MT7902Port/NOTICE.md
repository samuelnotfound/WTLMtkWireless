# Sources and licenses

This standalone MT7902 bootstrap port is derived from hbyonly1's
[itlmt7922-wifi](https://github.com/hbyonly1/itlmt7922-wifi) at commit
`20aaac5c8e1d97d737a73f1a684ded2ec1d0f940`. Its source and original contributor
notices are retained under the Clear BSD license in `LICENSE`. The foundation
itself derives from thanhnndev and contributors' macOS MediaTek driver. The exact
upstream notices and README are preserved in `docs/UPSTREAM-NOTICE.md` and
`docs/UPSTREAM-README.md`; their MT7922 hardware results do not apply to MT7902.

The MT7902 hardware reference and firmware source are
[morrownr/mt76](https://github.com/morrownr/mt76) at commit
`c539593ef5649ac855e0995f2f3492c42f4f32eb`. In particular, `mt7921/pci.c` and
`mt792x_dma.c` specify command ring 15, a shared 512-entry RX event ring without
the MCU-WA ring, and the MT7902 prefetch map. The original Linux source copyright
and license notices remain applicable; see `licenses/mt76-LICENSE.txt`.

The two unmodified MT7902 images in `fw/` are separately licensed by MediaTek in
`fw/LICENCE.mediatek`. Their SHA-256 pins are recorded in `fw/SHA256SUMS`. The
inherited BSD driver build embeds these images with assembly `.incbin`; they are
not re-licensed as GPL firmware. Release packages carry their license and hashes.

MacKernelSDK is pinned to `3f750085caa17ec3a7880f11c11bf4f48cd6a164` and is
obtained separately. This MediaTek driver subtree is licensed under Clear BSD;
no Intel driver code or GPL code from the earlier diagnostic starter is included.

Keep the source copyright notices, this file, LICENSE, and the firmware and
third-party license files with redistributed source and binaries.
