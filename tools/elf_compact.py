#!/usr/bin/env python3
"""B3 offset-preserving ELF32 compactor (non-runtime metadata only).

Copies an ELF32 little-endian executable and, without moving any byte:
  * zeroes the contents of allow-listed non-SHF_ALLOC sections (.pdr, .gnu.attributes,
    .mdebug.*, .comment, .symtab, .strtab, .debug_*, .shstrtab) and the section header table;
  * sets e_shoff = 0, e_shnum = 0, e_shstrndx = 0 (SHN_UNDEF).
Every zeroed range must lie outside the ELF header, the program header table and the file
range of every program header (any p_type). Anything unexpected is refused, never guessed.
File size, program headers and all program-header-covered bytes stay identical.

Usage: b3_compact.py --input IN --output OUT [--expect-sha256 HEX] [--payload BIN] [--manifest JSON]
Exit 0 = written; exit 1 = refused (E_* codes on stderr, no output written).
"""
import argparse, hashlib, json, os, struct, sys

ALLOW_NAMES = {'.pdr', '.gnu.attributes', '.comment', '.symtab', '.strtab', '.shstrtab'}
ALLOW_PREFIXES = ('.debug_', '.mdebug.')
# Section types that stay runtime-relevant regardless of name/flags: never touch.
RUNTIME_TYPES = {4: 'RELA', 5: 'HASH', 6: 'DYNAMIC', 9: 'REL', 11: 'DYNSYM',
                 14: 'INIT_ARRAY', 15: 'FINI_ARRAY', 16: 'PREINIT_ARRAY',
                 0x6ffffff6: 'GNU_HASH', 0x6ffffffd: 'VERDEF', 0x6ffffffe: 'VERNEED',
                 0x6fffffff: 'VERSYM', 0x7000000a: 'MIPS_ABIFLAGS', 0x70000006: 'MIPS_REGINFO'}
SHF_ALLOC, SHT_NOBITS = 0x2, 8
OFF_SHOFF, OFF_SHNUM, OFF_SHSTRNDX = 32, 48, 50


class Refuse(Exception):
    pass


def sha(b):
    return hashlib.sha256(b).hexdigest()


def overlaps(a0, a1, b0, b1):
    return a0 < b1 and b0 < a1


def plan(buf, payload=None):
    if len(buf) < 52 or buf[:4] != b'\x7fELF':
        raise Refuse('E_NOT_ELF: missing ELF magic')
    if buf[4] != 1 or buf[5] != 1:
        raise Refuse('E_CLASS: only ELF32 little-endian supported')
    (e_type, e_machine, _v, e_entry, e_phoff, e_shoff, _fl, e_ehsize, e_phentsize,
     e_phnum, e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from('<HHIIIIIHHHHHH', buf, 16)
    if e_ehsize != 52 or e_phentsize != 32 or e_phnum == 0:
        raise Refuse('E_HEADER: unexpected ehsize/phentsize/phnum %d/%d/%d' % (e_ehsize, e_phentsize, e_phnum))
    if e_shoff == 0 or e_shnum == 0:
        raise Refuse('E_NO_SHT: no section header table (already compact?)')
    if e_shentsize != 40 or e_shstrndx == 0 or e_shstrndx >= e_shnum:
        raise Refuse('E_SHT: bad shentsize/shstrndx')
    sht = (e_shoff, e_shoff + e_shnum * e_shentsize)
    if e_phoff + e_phnum * 32 > len(buf) or sht[1] > len(buf):
        raise Refuse('E_RANGE: header tables outside file')

    protected = [('ELF header', 0, 52), ('program header table', e_phoff, e_phoff + e_phnum * 32)]
    for i in range(e_phnum):
        p_type, p_off, _va, _pa, p_filesz = struct.unpack_from('<IIIII', buf, e_phoff + 32 * i)
        if p_off + p_filesz > len(buf):
            raise Refuse('E_RANGE: phdr %d file range outside file' % i)
        if p_filesz:
            protected.append(('phdr[%d] type 0x%x' % (i, p_type), p_off, p_off + p_filesz))

    secs = [struct.unpack_from('<IIIIIIIIII', buf, e_shoff + 40 * i) for i in range(e_shnum)]
    so, ss = secs[e_shstrndx][4], secs[e_shstrndx][5]
    if so + ss > len(buf):
        raise Refuse('E_RANGE: .shstrtab outside file')
    strtab = buf[so:so + ss]

    def name(n):
        end = strtab.find(b'\0', n)
        if n >= len(strtab) or end < 0:
            raise Refuse('E_SHT: bad section name offset %d' % n)
        return strtab[n:end].decode('ascii', 'replace')

    targets = []
    for i, s in enumerate(secs):
        if i == 0:
            continue
        sh_name, sh_type, sh_flags, _addr, sh_off, sh_size = s[0], s[1], s[2], s[3], s[4], s[5]
        nm = name(sh_name)
        allowed = nm in ALLOW_NAMES or nm.startswith(ALLOW_PREFIXES)
        if sh_flags & SHF_ALLOC:
            if allowed:
                raise Refuse('E_ALLOC_TARGET: allow-listed section %s is SHF_ALLOC' % nm)
            continue
        if not allowed:
            raise Refuse('E_UNKNOWN_NONALLOC: non-alloc section %r (type 0x%x) not allow-listed' % (nm, sh_type))
        if sh_type in RUNTIME_TYPES:
            raise Refuse('E_RUNTIME_TYPE: %s has runtime type %s' % (nm, RUNTIME_TYPES[sh_type]))
        if sh_type == SHT_NOBITS or sh_size == 0:
            continue
        if sh_off + sh_size > len(buf):
            raise Refuse('E_RANGE: %s outside file' % nm)
        targets.append((nm, sh_off, sh_off + sh_size))
    targets.append(('section-header-table', sht[0], sht[1]))

    for nm, a, b in targets:
        for pn, pa, pb in protected:
            if overlaps(a, b, pa, pb):
                raise Refuse('E_OVERLAP: %s [0x%x,0x%x) intersects %s [0x%x,0x%x)' % (nm, a, b, pn, pa, pb))

    if payload is not None:
        ph = sha(payload)
        hits = 0
        for i in range(e_phnum):
            p_type, p_off, _va, _pa, p_filesz = struct.unpack_from('<IIIII', buf, e_phoff + 32 * i)
            if p_type == 1 and p_filesz == len(payload) and sha(buf[p_off:p_off + p_filesz]) == ph:
                hits += 1
        if hits != 1:
            raise Refuse('E_PAYLOAD: expected exactly one PT_LOAD equal to payload, found %d' % hits)
    return targets, protected, (e_shoff, e_shnum, e_shstrndx)


def compact(buf, targets):
    out = bytearray(buf)
    for _n, a, b in targets:
        out[a:b] = bytes(b - a)
    struct.pack_into('<I', out, OFF_SHOFF, 0)
    struct.pack_into('<HH', out, OFF_SHNUM, 0, 0)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--input', required=True)
    ap.add_argument('--output', required=True)
    ap.add_argument('--expect-sha256')
    ap.add_argument('--payload')
    ap.add_argument('--manifest')
    a = ap.parse_args()
    try:
        if os.path.exists(a.output) or (a.manifest and os.path.exists(a.manifest)):
            raise Refuse('E_OUT_EXISTS: refusing to overwrite existing output')
        buf = open(a.input, 'rb').read()
        if a.expect_sha256 and sha(buf) != a.expect_sha256.lower():
            raise Refuse('E_INPUT_SHA: %s != expected %s' % (sha(buf), a.expect_sha256))
        payload = open(a.payload, 'rb').read() if a.payload else None
        targets, protected, sht = plan(buf, payload)
        out = compact(buf, targets)
        if len(out) != len(buf):
            raise Refuse('E_SIZE: output length changed')
        with open(a.output, 'wb') as f:
            f.write(out)
        man = {'input': a.input, 'output': a.output, 'input_sha256': sha(buf),
               'output_sha256': sha(out), 'size': len(buf),
               'orig_shoff': sht[0], 'orig_shnum': sht[1], 'orig_shstrndx': sht[2],
               'zeroed_ranges': [{'name': n, 'start': s, 'end': e, 'bytes': e - s} for n, s, e in targets],
               'total_zeroed_bytes': sum(e - s for _n, s, e in targets),
               'protected_ranges': [{'name': n, 'start': s, 'end': e} for n, s, e in protected]}
        if a.manifest:
            with open(a.manifest, 'w', newline='\n') as f:
                json.dump(man, f, indent=2)
        print(json.dumps({'status': 'OK', 'output_sha256': man['output_sha256'],
                          'zeroed_bytes': man['total_zeroed_bytes']}))
        return 0
    except Refuse as e:
        sys.stderr.write(str(e) + '\n')
        return 1


if __name__ == '__main__':
    sys.exit(main())
