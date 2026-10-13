#!/usr/bin/env python3
"""Independent, importable verifier for the U5 offline demo candidate.

verify_candidate(stock, out, payload, base, filesz, memsz) returns (ok, checks) where checks is a list
of (passed, message). It proves the candidate is the stock demo with ONLY the sanctioned changes:
  * ELF ehdr type/machine/entry/phoff/phnum/phentsize unchanged;
  * exactly ONE program header entry differs, which in the stock is PT_NULL and in the candidate is a
    well-formed PT_LOAD RWX for the payload (vaddr==base, filesz, memsz, page-congruent offset, valid
    non-overflowing geometry, no vaddr/file overlap with stock PT_LOADs);
  * within the stock length the ONLY byte differences are the 8-byte hook (`j base ; nop`) and that
    32-byte phdr entry;
  * the appended tail is exactly: zero padding up to the segment file offset, then the payload bytes,
    with NO extra trailing bytes.
Any geometry that is negative, overflows 32 bits, runs past EOF, or overlaps is refused.

CLI: python3 scripts/verify.py   (reads firmware demo, out/demo, payload/payload.bin, payload/sizes.txt)
"""
import hashlib, os, struct, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WS = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
STOCK = os.path.join(WS, "firmware", "rootfs", "release", "bin", "demo")
STOCK_SHA = "15ee04b552d287910d100313498e18715e0cf6ca7ba6433f2fdd8006e01ba2c9"
OUT = os.path.join(HERE, "out", "demo")
PAYLOAD = os.path.join(HERE, "payload", "payload.bin")
SIZES = os.path.join(HERE, "payload", "sizes.txt")

HOOK_OFF = 0x994b0
EXPECT = [0x3c1c0037, 0x279cc020, 0x0399e021, 0x1080012d]
PT_NULL, PT_LOAD = 0, 1
PF_R, PF_W, PF_X = 4, 2, 1
U32 = 0xFFFFFFFF


def _ehdr(b):
    (e_type, e_machine, _v, e_entry, e_phoff, _sh, _fl, _eh,
     e_phentsize, e_phnum) = struct.unpack_from("<HHIIIIIHHH", b, 16)
    return dict(type=e_type, machine=e_machine, entry=e_entry, phoff=e_phoff,
                phentsize=e_phentsize, phnum=e_phnum)


def _phdr(b, off):
    t, o, va, pa, fsz, msz, fl, al = struct.unpack_from("<IIIIIIII", b, off)
    return dict(type=t, off=o, vaddr=va, paddr=pa, filesz=fsz, memsz=msz, flags=fl, align=al)


def verify_candidate(stock, out, payload, base, filesz, memsz):
    checks = []
    def ck(cond, msg): checks.append((bool(cond), msg)); return bool(cond)

    if not ck(len(stock) > 64 and stock[:4] == b"\x7fELF", "stock is ELF"):
        return False, checks
    if not ck(len(out) > len(stock), "candidate longer than stock (payload appended)"):
        return False, checks

    se, oe = _ehdr(stock), _ehdr(out)
    ck(oe["type"] == se["type"] == 2, "ELF type EXEC unchanged")
    ck(oe["machine"] == se["machine"] == 8, "ELF machine MIPS unchanged")
    ck(oe["entry"] == se["entry"], "ELF entry unchanged")
    ck(oe["phoff"] == se["phoff"] and oe["phnum"] == se["phnum"]
       and oe["phentsize"] == se["phentsize"] == 32, "phdr table shape unchanged")
    if not all(c for c, _ in checks):
        return False, checks

    phoff, phnum = oe["phoff"], oe["phnum"]
    # exactly one phdr entry differs; it is the repurposed one
    diff_idx = [i for i in range(phnum)
                if out[phoff + i*32: phoff + i*32 + 32] != stock[phoff + i*32: phoff + i*32 + 32]]
    if not ck(len(diff_idx) == 1, f"exactly one phdr entry changed (got {len(diff_idx)})"):
        return False, checks
    pi = diff_idx[0]
    sp = _phdr(stock, phoff + pi*32)
    op = _phdr(out, phoff + pi*32)
    ck(sp["type"] == PT_NULL, "repurposed phdr was PT_NULL in stock")
    ck(op["type"] == PT_LOAD, "repurposed phdr is PT_LOAD in candidate")
    ck(op["flags"] == (PF_R | PF_W | PF_X), "new segment flags = RWX")
    ck(op["vaddr"] == base and op["paddr"] == base, "new segment vaddr == payload base")
    ck(op["filesz"] == filesz, "new segment filesz == payload length")
    ck(op["memsz"] == memsz, "new segment memsz == accepted manifest memsz")
    # geometry sanity (refuse negative / overflow / past-EOF)
    ck(0 < base and base + memsz <= U32, "vaddr geometry in 32-bit range")
    ck(0 <= filesz <= memsz, "0 <= filesz <= memsz (valid bss bounds)")
    ck(op["align"] >= 0x1000 and (op["align"] & (op["align"] - 1)) == 0, "p_align is a power of two >= page")
    ck(op["off"] % op["align"] == base % op["align"], "p_offset congruent to p_vaddr mod p_align")
    ck(op["off"] >= len(stock), "segment file data is appended after the stock image")
    ck(op["off"] + filesz <= len(out), "segment file range within the candidate (no past-EOF)")

    # no overlap with stock PT_LOADs (vaddr and file)
    for i in range(phnum):
        if i == pi:
            continue
        q = _phdr(stock, phoff + i*32)
        if q["type"] != PT_LOAD:
            continue
        ck(not (base < q["vaddr"] + q["memsz"] and q["vaddr"] < base + memsz),
           f"no vaddr overlap with stock PT_LOAD @0x{q['vaddr']:x}")
        ck(not (op["off"] < q["off"] + q["filesz"] and q["off"] < op["off"] + filesz),
           f"no file overlap with stock PT_LOAD off 0x{q['off']:x}")

    # hook words
    patched = list(struct.unpack_from("<4I", out, HOOK_OFF))
    j_word = 0x08000000 | ((base >> 2) & 0x03FFFFFF)
    ck(patched[0] == j_word and patched[1] == 0, f"hook = `j 0x{base:x} ; nop`")
    ck(patched[2] == EXPECT[2] and patched[3] == EXPECT[3], "hook words 2-3 (addu gp / beqz) untouched")
    # original stock hook words were the expected prologue
    orig = list(struct.unpack_from("<4I", stock, HOOK_OFF))
    ck(orig == EXPECT, "stock hook-site words were the expected prologue")

    # within-stock-length byte diffs are ONLY the hook (8) and the phdr entry (32)
    allowed = [(HOOK_OFF, HOOK_OFF + 8), (phoff + pi*32, phoff + pi*32 + 32)]
    bad = []
    k = 0
    L = len(stock)
    while k < L:
        if stock[k] != out[k]:
            j = k
            while j < L and stock[j] != out[j]:
                j += 1
            if not any(lo <= k and j <= hi for lo, hi in allowed):
                bad.append((k, j))
            k = j
        else:
            k += 1
    ck(not bad, "within stock length, only the hook and the phdr entry changed"
       + ("" if not bad else f"; unexpected {['0x%x..0x%x' % r for r in bad]}"))

    # exact tail: zero padding up to off, then payload, then nothing
    ck(all(b == 0 for b in out[len(stock):op["off"]]), "appended padding is all zero")
    ck(out[op["off"]:op["off"] + filesz] == payload, "appended bytes == payload.bin")
    ck(len(out) == op["off"] + filesz, "no extra trailing bytes after the payload")

    return all(c for c, _ in checks), checks


def _sizes():
    d = dict(l.strip().split("=") for l in open(SIZES) if "=" in l)
    return int(d["base"], 0), int(d["filesz"], 0), int(d["memsz"], 0)


def main():
    stock = open(STOCK, "rb").read()
    out = open(OUT, "rb").read()
    payload = open(PAYLOAD, "rb").read()
    base, filesz, memsz = _sizes()
    if hashlib.sha256(stock).hexdigest() != STOCK_SHA:
        print("FAIL stock demo sha256 != pinned baseline"); sys.exit(1)
    ok, checks = verify_candidate(stock, out, payload, base, filesz, memsz)
    for passed, msg in checks:
        print(("ok   " if passed else "FAIL ") + msg)
    print()
    print(f"stock     sha256 = {hashlib.sha256(stock).hexdigest()}")
    print(f"candidate sha256 = {hashlib.sha256(out).hexdigest()}")
    print("VERIFY OK" if ok else "VERIFY FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
