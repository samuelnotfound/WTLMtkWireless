#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Compare compiler-reported frames; these are individual frames, not complete kernel call chains."""
from pathlib import Path
r = Path(__file__).resolve().parents[1]
for label, directory in [('core', 'out/obj'), ('native', 'native/WTLWifiMenu/out/obj'),
                         ('standalone', 'experimental/MT7902Port/build/obj')]:
    rows = []
    for p in (r / directory).glob('*.su'):
        for line in p.read_text().splitlines():
            fields = line.rsplit('\t', 2)
            if len(fields) == 3 and fields[1].isdigit(): rows.append((int(fields[1]), fields[0], fields[2]))
    if not rows: raise SystemExit(f'Missing stack usage records for {label}')
    print(f'\n{label}: largest 60 individual frames in bytes (not call-chain totals)')
    for size, name, kind in sorted(rows, reverse=True)[:60]: print(f'{size:6d}\t{kind}\t{name}')
