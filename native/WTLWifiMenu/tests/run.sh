#!/bin/bash
# <wtl> project: mtkwlannative | file: tests/run.sh | author: watchthelight </wtl>
# Host tests (host clang with UBSan, not the kext flags). Writes only to ../out/tests/.
set -euo pipefail
cd "$(dirname "$0")"
OUT=../out/tests
mkdir -p "$OUT"
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined chanspec_test.c -o "$OUT/chanspec_test"
"$OUT/chanspec_test"
