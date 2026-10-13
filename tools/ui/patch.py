#!/usr/bin/env python3
"""Build the offline demo candidate: COPY the stock demo, append the U5 payload, repurpose the final
all-zero PT_NULL program header into a PT_LOAD for the payload, and patch the two hook words at
playset_equalizer_page_init (0x4994b0) to `j 0x820000 ; nop`.

make_candidate(stock, payload, base, filesz, memsz) is pure and importable: it RAISES ValueError on any
guard failure (wrong baseline sha, filesz mismatch, no usable PT_NULL, vaddr/file overlap, or the hook
site not holding the expected prologue) and otherwise returns (candidate_bytes, info). The CLI reads the
stock read-only and writes ONLY out/demo; it never mutates firmware or sibling task dirs, and on any
refusal it writes nothing.
"""
import hashlib, os, struct, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WS = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
STOCK = os.path.join(WS, "firmware", "rootfs", "release", "bin", "demo")
STOCK_SHA = "15ee04b552d287910d100313498e18715e0cf6ca7ba6433f2fdd8006e01ba2c9"
PAYLOAD = os.path.join(HERE, "payload", "payload.bin")
SIZES = os.path.join(HERE, "payload", "sizes.txt")
OUTDIR = os.path.join(HERE, "out")
OUT = os.path.join(OUTDIR, "demo")

HOOK_OFF = 0x994b0
PAYLOAD_VA = 0x00820000
SEG_ALIGN = 0x1000
EXPECT = [0x3c1c0037, 0x279cc020, 0x0399e021, 0x1080012d]
PT_NULL, PT_LOAD = 0, 1
PF_X, PF_W, PF_R = 1, 2, 4
U32 = 0xFFFFFFFF


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def make_candidate(stock, payload, base, filesz, memsz, stock_sha=STOCK_SHA):
    if sha256(stock) != stock_sha:
        raise ValueError(f"bad baseline: stock sha256 {sha256(stock)} != pinned {stock_sha}")
    if filesz != len(payload):
        raise ValueError(f"filesz {filesz} != payload length {len(payload)}")
    if not (0 < base and 0 <= filesz <= memsz and base + memsz <= U32):
        raise ValueError("invalid geometry (base/filesz/memsz)")
    if not (stock[:4] == b"\x7fELF" and stock[4] == 1 and stock[5] == 1):
        raise ValueError("stock is not ELF32 LE")
    (e_type, e_machine, _v, e_entry, e_phoff, _sh, _fl, _eh,
     e_phentsize, e_phnum) = struct.unpack_from("<HHIIIIIHHH", stock, 16)
    if e_type != 2 or e_machine != 8 or e_phentsize != 32:
        raise ValueError("expected ET_EXEC MIPS, phentsize 32")

    demo = bytearray(stock)
    loads, null_idx, null_off = [], None, None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        t, p_off, p_va, p_pa, p_fsz, p_msz, p_fl, p_al = struct.unpack_from("<IIIIIIII", demo, off)
        if t == PT_LOAD:
            loads.append((p_va, p_msz, p_off, p_fsz))
        if t == PT_NULL and null_idx is None:
            if any((p_off, p_va, p_pa, p_fsz, p_msz, p_fl)):
                raise ValueError("PT_NULL slot carries non-zero offset/addr/size/flags")
            null_idx, null_off = i, off
    if null_idx is None:
        raise ValueError("no PT_NULL program header to repurpose")

    # overlap (vaddr) with existing PT_LOADs
    pay_vend = base + memsz
    for (v, m, o, fs) in loads:
        if base < v + m and v < pay_vend:
            raise ValueError(f"payload vaddr overlaps PT_LOAD @0x{v:x}+0x{m:x}")

    # append at a page-congruent file offset
    app_off = (len(demo) + SEG_ALIGN - 1) & ~(SEG_ALIGN - 1)
    while app_off % SEG_ALIGN != base % SEG_ALIGN:
        app_off += 1
    pad = app_off - len(demo)
    demo += b"\x00" * pad
    demo += payload
    for (v, m, o, fs) in loads:                      # file-range overlap guard
        if app_off < o + fs and o < app_off + filesz:
            raise ValueError(f"payload file range overlaps PT_LOAD file off 0x{o:x}")

    struct.pack_into("<IIIIIIII", demo, null_off,
                     PT_LOAD, app_off, base, base, filesz, memsz, PF_R | PF_W | PF_X, SEG_ALIGN)

    orig = list(struct.unpack_from("<4I", demo, HOOK_OFF))
    if orig != EXPECT:
        raise ValueError("hook-site words != expected: " + " ".join(f"{w:08x}" for w in orig))
    j_word = 0x08000000 | ((base >> 2) & 0x03FFFFFF)
    struct.pack_into("<II", demo, HOOK_OFF, j_word, 0x00000000)

    info = dict(null_idx=null_idx, null_off=null_off, app_off=app_off, pad=pad,
                orig=orig, j_word=j_word, base=base, filesz=filesz, memsz=memsz)
    return bytes(demo), info


def _sizes():
    d = dict(l.strip().split("=") for l in open(SIZES) if "=" in l)
    return int(d["base"], 0), int(d["filesz"], 0), int(d["memsz"], 0)


def main():
    stock = open(STOCK, "rb").read()
    payload = open(PAYLOAD, "rb").read()
    base, filesz, memsz = _sizes()
    assert base == PAYLOAD_VA, "sizes.txt base disagrees with PAYLOAD_VA"
    try:
        demo, info = make_candidate(stock, payload, base, filesz, memsz)
    except ValueError as e:
        print(f"REFUSE: {e}")
        sys.exit(1)
    os.makedirs(OUTDIR, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(demo)
    print("PATCH OK")
    print(f"  out/demo sha256 = {sha256(demo)}")
    print(f"  out/demo bytes  = {len(demo)}")
    print("  changed ranges (file offsets):")
    print(f"    0x{HOOK_OFF:06x}..0x{HOOK_OFF+8:06x}  hook: {info['orig'][0]:08x} {info['orig'][1]:08x}"
          f" -> {info['j_word']:08x} 00000000")
    print(f"    0x{info['null_off']:06x}..0x{info['null_off']+32:06x}  phdr[{info['null_idx']}] "
          f"PT_NULL -> PT_LOAD RWX off=0x{info['app_off']:x} va=0x{base:x} filesz=0x{filesz:x} "
          f"memsz=0x{memsz:x} align=0x{SEG_ALIGN:x}")
    print(f"    0x{info['app_off']-info['pad']:06x}..0x{len(demo):06x}  appended {filesz} bytes "
          f"at file offset 0x{info['app_off']:x} (pad {info['pad']})")
    print(f"  j 0x{base:x} encodes as 0x{info['j_word']:08x}")


if __name__ == "__main__":
    main()
