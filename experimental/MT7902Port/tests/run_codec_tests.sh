#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/mt7902-codec.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    "$test_root/codec/mt7902_rx_codec.cpp" "$test_root/tests/test_rx_codec.cpp" \
    -o "$test_dir/test_rx_codec"
"$test_dir/test_rx_codec"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    "$test_root/codec/mt7902_rx_codec.cpp" "$test_root/codec/mt7902_tx_codec.cpp" \
    "$test_root/codec/mt7902_data_codec.cpp" "$test_root/codec/mt7902_ba_codec.cpp" \
    "$test_root/tests/test_data_tx_codec.cpp" \
    -o "$test_dir/test_data_tx_codec"
"$test_dir/test_data_tx_codec"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    "$test_root/codec/mt7902_scan_codec.cpp" "$test_root/tests/test_scan_codec.cpp" \
    -o "$test_dir/test_scan_codec"
"$test_dir/test_scan_codec"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    "$test_root/codec/mt7902_join_codec.cpp" "$test_root/codec/mt7902_tx_codec.cpp" \
    "$test_root/tests/test_join_codec.cpp" -o "$test_dir/test_join_codec"
"$test_dir/test_join_codec"
