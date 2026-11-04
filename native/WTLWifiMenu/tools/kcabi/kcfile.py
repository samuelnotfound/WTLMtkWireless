# <wtl> project: mtkwlannative | file: tools/kcabi/kcfile.py | author: watchthelight </wtl>
# SPDX-License-Identifier: ISC
# Project Legion: read-only reader for x86_64 macOS kernel collections (Mach-O MH_FILESET).
# Python 3 stdlib only. The approach (symbols from each fileset entry's LC_SYMTAB, vtables read from __ZTV symbols,
# chained kernel-cache pointers, class sizes from MetaClass constructors) follows the method documented by
# ga403wm-hackintosh (tools/kc.py, docs/IO80211-ABI-2.md; BSD-3-Clause-Clear, thanhnndev and contributors).
# This file is an independent rewrite for Project Legion; no code was copied.
"""Kernel collection reader.

    kc = KC('/System/Library/KernelCollections/BootKernelExtensions.kc')
    kc.entries                      [(bundle_id, vmaddr, fileoff)]
    kc.syms / kc.byname             {va: name} / {name: va}  (local + external N_SECT symbols of every entry)
    kc.vtable('IO80211Controller')  [(word, raw, target_va, name)]
    kc.metaclass_size('IO80211Controller')
    kc.carve(bundle_id, out_path)   standalone Mach-O of one entry, for llvm-objdump
"""
import bisect
import hashlib
import mmap
import os
import re
import struct

LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_FILESET_ENTRY = 0x80000035
OSMETACLASS_CTORS = ('__ZN11OSMetaClassC2EPKcPKS_j', '__ZN11OSMetaClassC1EPKcPKS_j')


class KC:
    def __init__(self, path):
        self.path = path
        self._f = open(path, 'rb')
        self.d = mmap.mmap(self._f.fileno(), 0, access=mmap.ACCESS_READ)
        self.segs = []          # (name, vmaddr, vmsize, fileoff, filesize, initprot, owner)
        self.entries = []       # (bundle_id, vmaddr, fileoff)
        self.entry_segs = {}    # bundle_id -> [seg]
        self.symtabs = []       # (owner, symoff, nsyms, stroff, strsize)
        self._load()
        self.base = min(s[1] for s in self.segs if s[2])
        self.syms, self.byname, self.sym_owner = {}, {}, {}
        self._load_syms()
        self.addrs = sorted(self.syms)

    # ------------------------------------------------------------------ Mach-O
    def _cmds(self, off):
        magic, _cpu, _sub, _ft, ncmds, _sz, _fl, _r = struct.unpack_from('<IIIIIIII', self.d, off)
        if magic != 0xfeedfacf:
            raise ValueError('not a 64-bit Mach-O at %#x' % off)
        p = off + 32
        for _ in range(ncmds):
            cmd, size = struct.unpack_from('<II', self.d, p)
            yield cmd, p, size
            p += size

    def _seg(self, p, owner):
        name = self.d[p + 8:p + 24].split(b'\0')[0].decode()
        vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', self.d, p + 24)
        _maxprot, initprot = struct.unpack_from('<ii', self.d, p + 56)
        return (name, vmaddr, vmsize, fileoff, filesize, initprot, owner)

    def _load(self):
        for cmd, p, _size in self._cmds(0):
            if cmd == LC_SEGMENT_64:
                self.segs.append(self._seg(p, '<kc>'))
            elif cmd == LC_SYMTAB:
                self.symtabs.append(('<kc>',) + struct.unpack_from('<IIII', self.d, p + 8))
            elif cmd == LC_FILESET_ENTRY:
                vmaddr, fileoff, strx, _ = struct.unpack_from('<QQII', self.d, p + 8)
                end = self.d.find(b'\0', p + strx)
                self.entries.append((self.d[p + strx:end].decode(), vmaddr, fileoff))
        for bid, _vm, fo in self.entries:
            for cmd, p, _size in self._cmds(fo):
                if cmd == LC_SYMTAB:
                    so, ns, sto, sts = struct.unpack_from('<IIII', self.d, p + 8)
                    if ns:
                        self.symtabs.append((bid, so, ns, sto, sts))
                elif cmd == LC_SEGMENT_64:
                    self.entry_segs.setdefault(bid, []).append(self._seg(p, bid))

    def _load_syms(self):
        for owner, so, ns, sto, sts in self.symtabs:
            for i in range(ns):
                strx, typ, _sect, _desc, val = struct.unpack_from('<IBBHQ', self.d, so + i * 16)
                if not val or strx >= sts or (typ & 0xe0) or (typ & 0x0e) != 0x0e:
                    continue
                end = self.d.find(b'\0', sto + strx)
                nm = self.d[sto + strx:end].decode('latin1')
                if val not in self.syms or (nm.startswith('__Z') and not self.syms[val].startswith('__Z')):
                    self.syms[val] = nm
                    self.sym_owner[val] = owner
                self.byname.setdefault(nm, val)

    def sha256(self):
        h = hashlib.sha256()
        for i in range(0, len(self.d), 1 << 24):
            h.update(self.d[i:i + (1 << 24)])
        return h.hexdigest()

    # ------------------------------------------------------------------ addresses
    def va2off(self, va):
        for _n, vm, sz, fo, fs, _p, _o in self.segs:
            if vm <= va < vm + sz and va - vm < fs:
                return fo + (va - vm)
        return None

    def read(self, va, n):
        o = self.va2off(va)
        return None if o is None else bytes(self.d[o:o + n])

    def q(self, va):
        b = self.read(va, 8)
        return struct.unpack('<Q', b)[0] if b and len(b) == 8 else None

    def owner(self, va):
        for bid, segs in self.entry_segs.items():
            for s in segs:
                if s[1] <= va < s[1] + s[2]:
                    return bid
        return None

    def symname(self, va):
        i = bisect.bisect_right(self.addrs, va) - 1
        if i < 0:
            return None
        a = self.addrs[i]
        return self.syms[a] if a == va else '%s+%#x' % (self.syms[a], va - a)

    def next_sym(self, va):
        i = bisect.bisect_right(self.addrs, va)
        return self.addrs[i] if i < len(self.addrs) else None

    def find(self, name):
        return self.byname.get(name)

    def find_prefix(self, prefix):
        return sorted((v, n) for n, v in self.byname.items() if n.startswith(prefix))

    # ------------------------------------------------------------------ chained kernel-cache pointers
    def ptr(self, raw):
        """DYLD_CHAINED_PTR_X86_64_KERNEL_CACHE: low 30 bits = offset from the collection base (cacheLevel 0)."""
        if not raw:
            return 0
        if raw >> 40 == 0xffffff:
            return raw
        return self.base + (raw & 0x3fffffff)

    # ------------------------------------------------------------------ C++
    def vtable(self, cls):
        va = self.find('__ZTV%d%s' % (len(cls), cls))
        if va is None:
            return None
        end = self.next_sym(va) or va + 8
        out = []
        for i in range((end - va) // 8):
            raw = self.q(va + i * 8)
            if raw is None:
                break
            tgt = self.ptr(raw)
            out.append((i, raw, tgt, (self.syms.get(tgt) or '%#x' % tgt) if tgt else '-'))
        return out

    def methods(self, cls):
        tag = re.compile(r'^__ZNK?%d%s(?=\d|C[12]E|D[012]E|[a-z])' % (len(cls), re.escape(cls)))
        return [(a, self.syms[a]) for a in self.addrs if tag.match(self.syms[a])]

    def classes(self):
        out = set()
        for nm in self.byname:
            m = re.match(r'^__ZN(\d+)(\w+?)10gMetaClassE$', nm)
            if m and int(m.group(1)) == len(m.group(2)):
                out.add(m.group(2))
            m = re.match(r'^__ZTV(\d+)(\w+)$', nm)
            if m and int(m.group(1)) == len(m.group(2)):
                out.add(m.group(2))
        return sorted(out)

    def metaclass_size(self, cls):
        """Instance size passed in ECX to OSMetaClass::OSMetaClass(name, super, size) by <cls>::MetaClass::MetaClass()."""
        ctors = {self.find(n) for n in OSMETACLASS_CTORS} - {None}
        for kind in ('C2Ev', 'C1Ev'):
            va = self.find('__ZN%d%s9MetaClass%s' % (len(cls), cls, kind))
            if va is None:
                continue
            end = self.next_sym(va) or va + 0x80
            b = self.read(va, min(end - va, 0x200)) or b''
            ecx = None
            for i in range(len(b)):
                if b[i] == 0xb9 and i + 5 <= len(b):                     # mov ecx, imm32
                    ecx = struct.unpack_from('<I', b, i + 1)[0]
                elif b[i] == 0xe8 and i + 5 <= len(b) and ecx is not None:   # call rel32
                    tgt = va + i + 5 + struct.unpack_from('<i', b, i + 1)[0]
                    if tgt in ctors or (self.syms.get(tgt, '').startswith('__ZN11OSMetaClassC')):
                        return ecx
        return None

    # ------------------------------------------------------------------ carving
    def carve(self, bid, out_path):
        """Write a copy of the collection whose first bytes are <bid>'s own Mach-O header, so llvm-objdump sees that
        entry as a normal binary (its load commands use absolute file offsets into the collection)."""
        fo = next(e[2] for e in self.entries if e[0] == bid)
        hsz = 32 + struct.unpack_from('<I', self.d, fo + 20)[0]
        if os.path.exists(out_path) and os.path.getsize(out_path) == len(self.d):
            with open(out_path, 'rb') as f:
                if f.read(hsz) == self.d[fo:fo + hsz]:
                    return out_path
        with open(out_path, 'wb') as f:
            f.write(self.d[fo:fo + hsz])
            f.write(self.d[hsz:])
        return out_path
