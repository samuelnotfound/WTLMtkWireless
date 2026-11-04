# <wtl> project: mtkwlannative | file: tools/kcabi/disasm.py | author: watchthelight </wtl>
# SPDX-License-Identifier: ISC
# Project Legion: x86_64 disassembly of kernel-collection functions through llvm-objdump (MacPorts llvm-20).
# Output line format matches the ga403wm 24H32 dumps ("<addr>: <mnemonic> <operands>  ; <note>") so the two
# builds can be diffed line by line after normalisation (see normalise()).
import os
import re
import subprocess

OBJDUMP_CANDIDATES = (
    os.environ.get('LLVM_OBJDUMP') or '',
    '/opt/local/libexec/llvm-20/bin/llvm-objdump',
    '/opt/local/bin/llvm-objdump-mp-20',
)
LINE = re.compile(r'^\s*([0-9a-f]{8,16}):\s+(.*)$')
RIP = re.compile(r'\[rip ([+-]) (0x[0-9a-f]+|\d+)\]')
BIG_ENTRIES = ('com.apple.kernel',)       # disassembled per function instead of as a whole


def objdump():
    for p in OBJDUMP_CANDIDATES:
        if p and os.path.exists(p):
            return p
    raise SystemExit('llvm-objdump not found (set LLVM_OBJDUMP)')


class Disassembler:
    def __init__(self, kc, cache_dir):
        self.kc = kc
        self.cache = cache_dir
        os.makedirs(cache_dir, exist_ok=True)
        self.whole = {}     # bid -> (sorted addr list, {addr: text})

    def _carved(self, bid):
        return self.kc.carve(bid, os.path.join(self.cache, bid + '.macho'))

    def _parse(self, text):
        addrs, ins = [], {}
        for line in text.splitlines():
            m = LINE.match(line)
            if not m:
                continue
            a = int(m.group(1), 16)
            body = m.group(2).split('\t', 1)
            body = body[1] if len(body) > 1 and body[0].strip() == '' else m.group(2)
            body = re.sub(r'\s+(#|##)\s.*$', '', body)            # objdump's own comments
            body = re.sub(r'\s+<[^>]*>$', '', body)               # objdump's own symbol guess
            parts = body.strip().split(None, 1)
            if not parts:
                continue
            addrs.append(a)
            ins[a] = (parts[0], parts[1] if len(parts) > 1 else '')
        return addrs, ins

    def _load_whole(self, bid):
        if bid not in self.whole:
            path = self._carved(bid)
            out = subprocess.run([objdump(), '-d', '-M', 'intel', '--no-show-raw-insn', path],
                                 capture_output=True, text=True).stdout
            self.whole[bid] = self._parse(out)
        return self.whole[bid]

    def _range(self, bid, start, end):
        if bid in BIG_ENTRIES:
            path = self._carved(bid)
            out = subprocess.run([objdump(), '-d', '-M', 'intel', '--no-show-raw-insn',
                                  '--start-address=%#x' % start, '--stop-address=%#x' % end, path],
                                 capture_output=True, text=True).stdout
            addrs, ins = self._parse(out)
        else:
            addrs, ins = self._load_whole(bid)
            import bisect
            i, j = bisect.bisect_left(addrs, start), bisect.bisect_left(addrs, end)
            addrs = addrs[i:j]
        return addrs, ins

    def note(self, addr, nxt, mnem, ops):
        kc = self.kc
        m = RIP.search(ops)
        if m and nxt:
            disp = int(m.group(2), 0) * (1 if m.group(1) == '+' else -1)
            tgt = nxt + disp
            s = kc.read(tgt, 80)
            if s:
                z = s.split(b'\0')[0]
                if len(z) >= 4 and all(0x20 <= c < 0x7f for c in z):
                    return '"%s"' % z.decode()
            nm = kc.symname(tgt)
            return '%#x %s' % (tgt, nm) if nm and '+' not in nm else '%#x' % tgt
        if mnem in ('call', 'jmp') or mnem.startswith('j'):
            m = re.match(r'^(0x[0-9a-f]+)$', ops.strip())
            if m:
                nm = kc.symname(int(m.group(1), 16))
                if nm:
                    return nm
        return ''

    def function(self, va, end=None):
        """List of text lines for [va, end) (end = next symbol by default)."""
        bid = self.kc.owner(va)
        if bid is None:
            return None
        end = end or self.kc.next_sym(va) or va + 0x400
        addrs, ins = self._range(bid, va, end)
        out = []
        for k, a in enumerate(addrs):
            mnem, ops = ins[a]
            nxt = addrs[k + 1] if k + 1 < len(addrs) else end
            n = self.note(a, nxt, mnem, ops)
            out.append('%x: %-8s %s%s' % (a, mnem, ops, ('  ; ' + n) if n else ''))
        return out


NUM = re.compile(r'(?<![\w.])(?:0x[0-9a-f]+|\d+)(?![\w.])')
ADDR = re.compile(r'0xffffff[0-9a-f]{10}|\b[0-9a-f]{16}\b|\+0x[0-9a-f]+\b')


def normalise(lines):
    """Strip addresses so two builds compare by instruction stream, operands, offsets and called names."""
    out = []
    for ln in lines:
        if ln.startswith('#'):
            continue
        m = re.match(r'^[0-9a-f]+:\s+(\S+)\s*(.*)$', ln)
        if not m:
            continue
        mnem, rest = m.group(1), m.group(2)
        ops, _, note = rest.partition('  ; ')
        ops = RIP.sub('[rip+X]', ops)
        if mnem.startswith('j') or mnem == 'call':
            ops = re.sub(r'^0x[0-9a-f]+$', 'T', ops.strip())
        if mnem.startswith('j'):
            note = ''                                     # local branch targets: capstone dumps carry no name
        note = ADDR.sub('', note).strip()
        ops = NUM.sub(lambda m: '%#x' % int(m.group(0), 0) if int(m.group(0), 0) else '0', ops)
        # capstone (24H32 dumps) and llvm-objdump differ in a few spellings
        ops = ops.replace('xmmword ptr ', '').replace('  ', ' ').strip()
        out.append('%s %s %s' % (mnem, ops, note))
    return out
