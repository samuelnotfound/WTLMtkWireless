#!/usr/bin/env python3
# <wtl> project: mtkwlannative | file: tools/kcabi/compare.py | author: watchthelight </wtl>
# SPDX-License-Identifier: ISC
# Project Legion: compare two native Wi-Fi ABI dumps (for example ga403wm's 24H32 tree and our 25G241 dump) and write
# what changed: instance sizes, vtable slot positions by method, apple80211 GET/SET request tables, and which
# disassembled functions differ (with the field offsets, virtual-call slots and callees that appear or disappear).
"""
usage: compare.py OLD_DIR NEW_DIR [--out DIR] [--stubs itlmt7925_iface_stubs.inc]

OLD_DIR may be a tree with round*/ subfolders (files are found recursively; the first hit wins, top level first).
Writes into NEW_DIR/compare (or --out): summary.txt, sizes.txt, vtables/<Class>.txt, dispatch.txt, functions.txt,
native-slots.txt.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from disasm import normalise  # noqa: E402


def walk(root, suffix):
    found = {}
    for d, _dirs, files in sorted(os.walk(root), key=lambda x: (x[0].count(os.sep), x[0])):
        if os.sep + 'compare' in d[len(root):]:
            continue
        for fn in files:
            if fn.endswith(suffix) and fn not in found:
                found[fn] = os.path.join(d, fn)
    return found


def read_vtable(path):
    rows = {}
    for line in open(path):
        p = line.rstrip('\n').split('\t')
        if len(p) == 3 and p[0].isdigit():
            rows[int(p[0])] = p[2]
    return rows


def method_key(sym):
    """Mangled method name + parameters without the owning class: '__ZNK17IO80211Controller12getWorkQueueEv' ->
    'K12getWorkQueueEv'. Destructors/ctors keep their D0/D1 code. Non-C++ entries are returned unchanged."""
    m = re.match(r'^__ZN(K?)(\d+)', sym)
    if not m:
        return sym
    k, i = m.group(1), 4 + len(m.group(1))
    parts = []
    while i < len(sym) and sym[i].isdigit():
        j = i
        while sym[j].isdigit():
            j += 1
        ln = int(sym[i:j])
        parts.append(sym[j:j + ln])
        i = j + ln
    rest = sym[i:]
    if re.match(r'^(C[123]|D[012])E', rest):
        return k + rest
    if len(parts) >= 2:
        return k + '%d%s' % (len(parts[-1]), parts[-1]) + rest
    return sym


def asm_functions(root):
    """{symbol: [lines]} for every *.asm.txt whose first line is '# 0x<addr> <symbol>'."""
    out = {}
    for fn, path in walk(root, '.asm.txt').items():
        with open(path, errors='replace') as f:
            first = f.readline()
            m = re.match(r'^# (0x[0-9a-f]+)\s+(_\S+)', first)
            if m and m.group(2) not in out:
                out[m.group(2)] = [first] + f.readlines()
    return out


def metaclass_sizes(funcs):
    sizes = {}
    for sym, lines in funcs.items():
        m = re.match(r'^__ZN(\d+)(\w+)9MetaClassC[12]Ev$', sym)
        if not m or int(m.group(1)) != len(m.group(2)):
            continue
        ecx = None
        for ln in lines:
            mm = re.search(r'mov\s+ecx, (0x[0-9a-f]+|\d+)', ln)
            if mm:
                ecx = int(mm.group(1), 0)
            if 'OSMetaClassC' in ln and ecx is not None:
                sizes[m.group(2)] = ecx
                break
    return sizes


def features(lines):
    vcalls, fields, imms, callees = set(), set(), set(), set()
    for ln in lines:
        m = re.match(r'^[0-9a-f]+:\s+(\S+)\s*(.*)$', ln)
        if not m:
            continue
        mnem, rest = m.group(1), m.group(2)
        ops, _, note = rest.partition('  ; ')
        if mnem in ('call', 'jmp'):
            mm = re.search(r'\[(r\w+) \+ (0x[0-9a-f]+)\]', ops)
            if mm and 'rip' not in ops:
                vcalls.add(int(mm.group(2), 16))
            elif note.startswith('_'):
                callees.add(note.split('+')[0].strip())
        else:
            for mm in re.finditer(r'\[(r(?!ip)\w+|e\w+)(?: \+ \w+\*\d)? ([+-]) (0x[0-9a-f]+)\]', ops):
                fields.add(int(mm.group(3), 16) * (1 if mm.group(2) == '+' else -1))
            for mm in re.finditer(r'(?<![\[\w+-] )(?<!\+ )(?:, |^)(0x[0-9a-f]+|-?\d+)$', ops):
                v = int(mm.group(1), 0)
                if abs(v) < (1 << 32) and abs(v) > 1:
                    imms.add(v)
    return vcalls, fields, imms, callees


def fmt(s, slot=False):
    s = sorted(s)
    if slot:
        return ' '.join('%#x(slot %d)' % (v, (v + 0x10) // 8) for v in s)
    return ' '.join(('%#x' % v) if v >= 0 else ('-%#x' % -v) for v in s)


def write(path, lines):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('old')
    ap.add_argument('new')
    ap.add_argument('--out')
    ap.add_argument('--stubs')
    a = ap.parse_args()
    out = a.out or os.path.join(a.new, 'compare')
    summary = ['# ABI comparison', '# old: %s' % a.old, '# new: %s' % a.new, '']

    # ---- sizes
    of, nf = asm_functions(a.old), asm_functions(a.new)
    osz, nsz = metaclass_sizes(of), metaclass_sizes(nf)
    lines = ['# class\told\tnew\tdelta']
    for c in sorted(set(osz) & set(nsz)):
        d = nsz[c] - osz[c]
        lines.append('%s\t%#x\t%#x\t%s' % (c, osz[c], nsz[c], ('%+#x' % d) if d else 'same'))
    write(os.path.join(out, 'sizes.txt'), lines)
    summary.append('## instance sizes (MetaClass constructors)')
    summary += ['  ' + l for l in lines[1:] if not l.endswith('same')] or ['  all unchanged']
    summary.append('')

    # ---- vtables
    ov, nv = walk(a.old, '.vtable.txt'), walk(a.new, '.vtable.txt')
    summary.append('## vtables (slot = word index from the __ZTV symbol; words 0-1 are the ABI header)')
    for fn in sorted(set(ov) & set(nv)):
        cls = fn[:-len('.vtable.txt')]
        o, n = read_vtable(ov[fn]), read_vtable(nv[fn])
        ok = {}
        for i, s in o.items():
            if s.startswith('__Z'):
                ok.setdefault(method_key(s), i)
        nk = {}
        for i, s in n.items():
            if s.startswith('__Z'):
                nk.setdefault(method_key(s), i)
        first_div = next((i for i in sorted(o) if i in n and method_key(o[i]) != method_key(n[i])), None)
        moved = [(k, ok[k], nk[k]) for k in ok if k in nk and ok[k] != nk[k]]
        gone = [(k, ok[k]) for k in ok if k not in nk]
        added = [(k, nk[k]) for k in nk if k not in ok]
        rows = ['# %s: old %d words, new %d words; first slot whose method differs: %s' %
                (cls, len(o), len(n), first_div)]
        rows += ['moved\t%s\t%d -> %d' % (k, x, y) for k, x, y in sorted(moved, key=lambda t: t[1])]
        rows += ['removed\t%s\t%d' % (k, x) for k, x in sorted(gone, key=lambda t: t[1])]
        rows += ['added\t%s\t%d' % (k, y) for k, y in sorted(added, key=lambda t: t[1])]
        write(os.path.join(out, 'vtables', cls + '.txt'), rows)
        tag = 'unchanged' if not (moved or gone or added) and len(o) == len(n) else \
            'words %d->%d, first diff slot %s, %d moved, %d removed, %d added' % (
                len(o), len(n), first_div, len(moved), len(gone), len(added))
        summary.append('  %-36s %s' % (cls, tag))
    summary.append('')

    # ---- dispatch
    od, nd = walk(a.old, '-dispatch.txt'), walk(a.new, '-dispatch.txt')
    drows = []
    summary.append('## apple80211 request tables (apple80211RouteInternal)')
    for fn in ('ioctl-get-dispatch.txt', 'ioctl-set-dispatch.txt'):
        if fn not in od or fn not in nd:
            continue

        def table(p):
            t = {}
            for line in open(p):
                q = line.rstrip('\n').split('\t')
                if len(q) == 4 and q[0].isdigit():
                    t[int(q[0])] = q[3]
            return t
        o, n = table(od[fn]), table(nd[fn])
        ch = [i for i in sorted(set(o) | set(n)) if o.get(i, '-') != n.get(i, '-')]
        drows.append('# %s: old IDs 1..%d, new IDs 1..%d, %d IDs differ' % (fn, max(o), max(n), len(ch)))
        for i in ch:
            drows.append('%d\t%#x\t%s\t->\t%s' % (i, i, o.get(i, '(none)'), n.get(i, '(none)')))
        summary.append('  %s: old 1..%d, new 1..%d, %d IDs differ (new IDs or changed handler)' % (fn, max(o), max(n), len(ch)))
    write(os.path.join(out, 'dispatch.txt'), drows)
    summary.append('')

    # ---- functions
    same, changed, frows = 0, 0, []
    for sym in sorted(set(of) & set(nf)):
        a1, b1 = normalise(of[sym]), normalise(nf[sym])
        if a1 == b1:
            same += 1
            frows.append('same\t%s' % sym)
            continue
        changed += 1
        fa, fb = features(of[sym]), features(nf[sym])
        parts = []
        for name, x, y, sl in (('vcall', fa[0], fb[0], True), ('field', fa[1], fb[1], False),
                               ('imm', fa[2], fb[2], False), ('callee', fa[3], fb[3], None)):
            if x != y:
                if sl is None:
                    parts.append('%s -[%s] +[%s]' % (name, ' '.join(sorted(x - y)), ' '.join(sorted(y - x))))
                else:
                    parts.append('%s -[%s] +[%s]' % (name, fmt(x - y, sl), fmt(y - x, sl)))
        frows.append('changed\t%s\t%d->%d insns\t%s' % (sym, len(a1), len(b1), '; '.join(parts) or 'same features (addresses or order only)'))
    only_old = sorted(set(of) - set(nf))
    frows += ['old-only\t%s' % s for s in only_old]
    write(os.path.join(out, 'functions.txt'), ['# status\tsymbol\tdetail (vcall offsets are [reg+N] in call operands)'] + frows)
    summary.append('## disassembled functions present in both: %d identical after normalisation, %d changed, %d old-only'
                   % (same, changed, len(only_old)))
    summary.append('')

    # ---- native stubs (itlmt7925Native interface overrides)
    if a.stubs and os.path.exists(a.stubs):
        bcm_new = read_vtable(nv['AppleBCMWLANSkywalkInterface.vtable.txt'])
        new_by_name = {}
        for i, s in bcm_new.items():
            m = re.search(r'\d+([A-Za-z_][A-Za-z0-9_]*)E', s[3:]) if s.startswith('__Z') else None
            new_by_name.setdefault(method_key(s), i)
        bcm_old = read_vtable(ov['AppleBCMWLANSkywalkInterface.vtable.txt'])
        rows = ['# itlmt7925Native interface override: old slot (from the stub file) -> slot of the same method in the new'
                ' AppleBCMWLANSkywalkInterface vtable']
        moved = 0
        for line in open(a.stubs):
            m = re.search(r'\b(\w+)\(.*override;\s*// slot (\d+)', line)
            if not m:
                continue
            old_slot = int(m.group(2))
            key = method_key(bcm_old.get(old_slot, ''))
            ns = new_by_name.get(key)
            if ns != old_slot:
                moved += 1
            rows.append('%s\t%d\t%s\t%s' % (m.group(1), old_slot, ns if ns is not None else 'GONE', key))
        write(os.path.join(out, 'native-slots.txt'), rows)
        summary.append('## itlmt7925Native interface stubs: %d of %d declarations change slot (or vanish) on the new build'
                       % (moved, len(rows) - 1))
    write(os.path.join(out, 'summary.txt'), summary)
    print('\n'.join(summary))


if __name__ == '__main__':
    main()
