// SPDX-License-Identifier: BSD-3-Clause-Clear
// MediaTek firmware, redistributable under experimental/MT7902Port/fw/LICENCE.mediatek.
__asm__(".section __DATA,__const\n.balign 16\n"
        ".globl _itl_fw_patch\n_itl_fw_patch:\n"
        ".incbin \"experimental/MT7902Port/fw/WIFI_MT7902_patch_mcu_1_1_hdr.bin\"\n"
        ".globl _itl_fw_patch_end\n_itl_fw_patch_end:\n.balign 16\n"
        ".globl _itl_fw_ram\n_itl_fw_ram:\n"
        ".incbin \"experimental/MT7902Port/fw/WIFI_RAM_CODE_MT7902_1.bin\"\n"
        ".globl _itl_fw_ram_end\n_itl_fw_ram_end:\n.byte 0\n.text\n");
