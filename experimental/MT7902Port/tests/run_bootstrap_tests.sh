#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause-Clear
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/mt7902-bootstrap.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    "$test_root/tests/test_bootstrap.cpp" -o "$test_dir/test_bootstrap"
"$test_dir/test_bootstrap" "$test_root/fw/WIFI_MT7902_patch_mcu_1_1_hdr.bin" \
    "$test_root/fw/WIFI_RAM_CODE_MT7902_1.bin"
